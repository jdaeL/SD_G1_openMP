/**
 * ============================================================
 *  Análisis de Tasa de Recompra por Departamento - Instacart
 *  Proyecto: Sistemas Distribuidos - Computación Paralela
 *
 *  Compilación:
 *    g++ -std=c++17 -O2 -fopenmp -o reorder_analysis main.cpp
 *
 *  Uso:
 *    ./reorder_analysis
 *    (Los archivos CSV deben estar en el directorio de trabajo)
 * ============================================================
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <iomanip>
#include <algorithm>
#include <omp.h>

// ============================================================
// ESTRUCTURAS DE DATOS
// ============================================================

/** Representa una fila del archivo order_products.csv */
struct OrdenProducto {
    int order_id;
    int product_id;
    int add_to_cart_order;
    int reordered; // 1 = recompra, 0 = primera vez
};

/** Acumula métricas por departamento para el cálculo final */
struct MetricaDepartamento {
    long long total_vendidos  = 0;
    long long total_reordenes = 0;
};

// ============================================================
// TIPOS ALIAS (legibilidad)
// ============================================================

// department_id -> nombre del departamento
using MapaDepartamentos = std::unordered_map<int, std::string>;

// product_id -> department_id
using MapaProductos = std::unordered_map<int, int>;

// department_id -> métricas acumuladas
using MapaMetricas = std::unordered_map<int, MetricaDepartamento>;

// ============================================================
// FASE 1: CARGA DE DATOS RELACIONALES
// ============================================================

/**
 * Lee departments.csv y construye un mapa id -> nombre.
 * Formato esperado: department_id,department
 */
MapaDepartamentos cargarDepartamentos(const std::string& ruta) {
    MapaDepartamentos mapa;
    std::ifstream archivo(ruta);

    if (!archivo.is_open()) {
        std::cerr << "[ERROR] No se pudo abrir: " << ruta << "\n";
        return mapa;
    }

    std::string linea;
    std::getline(archivo, linea); // Saltar cabecera

    while (std::getline(archivo, linea)) {
        if (linea.empty()) continue;
        std::istringstream ss(linea);
        std::string campo;

        std::getline(ss, campo, ',');
        int dept_id = std::stoi(campo);

        std::getline(ss, campo);
        mapa[dept_id] = campo;
    }

    std::cout << "[OK] Departamentos cargados: " << mapa.size() << "\n";
    return mapa;
}

/**
 * Lee products.csv y construye un mapa product_id -> department_id.
 * Formato esperado: product_id,product_name,aisle_id,department_id
 * Nota: product_name puede contener comas; por eso parseamos por posición.
 */
MapaProductos cargarProductos(const std::string& ruta) {
    MapaProductos mapa;
    std::ifstream archivo(ruta);

    if (!archivo.is_open()) {
        std::cerr << "[ERROR] No se pudo abrir: " << ruta << "\n";
        return mapa;
    }

    std::string linea;
    std::getline(archivo, linea); // Saltar cabecera

    while (std::getline(archivo, linea)) {
        if (linea.empty()) continue;

        // Extraemos product_id (primera columna)
        size_t pos1 = linea.find(',');
        if (pos1 == std::string::npos) continue;
        int product_id = std::stoi(linea.substr(0, pos1));

        // Saltamos product_name (puede contener comas; buscamos desde el final)
        // Las últimas dos columnas son: aisle_id,department_id
        size_t pos_ultimo = linea.rfind(',');
        if (pos_ultimo == std::string::npos || pos_ultimo == pos1) continue;
        int department_id = std::stoi(linea.substr(pos_ultimo + 1));

        mapa[product_id] = department_id;
    }

    std::cout << "[OK] Productos cargados: " << mapa.size() << "\n";
    return mapa;
}

// ============================================================
// FASE 2: LECTURA MASIVA DE order_products.csv
// ============================================================

/**
 * Lee todos los registros de order_products.csv hacia un vector de structs.
 * Reservamos capacidad previa para evitar realocaciones costosas.
 */
std::vector<OrdenProducto> cargarOrdenes(const std::string& ruta,
                                          size_t reserva_inicial = 35'000'000) {
    std::vector<OrdenProducto> ordenes;
    ordenes.reserve(reserva_inicial);

    std::ifstream archivo(ruta);
    if (!archivo.is_open()) {
        std::cerr << "[ERROR] No se pudo abrir: " << ruta << "\n";
        return ordenes;
    }

    std::string linea;
    std::getline(archivo, linea); // Saltar cabecera

    while (std::getline(archivo, linea)) {
        if (linea.empty()) continue;

        OrdenProducto op;
        char sep;
        // Usamos sscanf para parseo rápido de 4 enteros separados por comas
        if (sscanf(linea.c_str(), "%d,%d,%d,%d",
                   &op.order_id, &op.product_id,
                   &op.add_to_cart_order, &op.reordered) == 4) {
            ordenes.push_back(op);
        }
    }

    std::cout << "[OK] Registros de órdenes cargados: " << ordenes.size() << "\n";
    return ordenes;
}

// ============================================================
// FASE 3: PROCESAMIENTO SECUENCIAL (Baseline)
// ============================================================

/**
 * Recorre el vector de órdenes de forma secuencial.
 * Para cada registro, obtiene el department_id mediante el mapa de productos
 * y acumula totales y reordenes por departamento.
 *
 * @return Mapa de métricas por department_id
 */
MapaMetricas procesarSecuencial(const std::vector<OrdenProducto>& ordenes,
                                 const MapaProductos& mapa_productos) {
    MapaMetricas resultado;

    for (const auto& op : ordenes) {
        // Búsqueda O(1) en el mapa de productos
        auto it = mapa_productos.find(op.product_id);
        if (it == mapa_productos.end()) continue;

        int dept_id = it->second;
        auto& m = resultado[dept_id];
        m.total_vendidos++;
        m.total_reordenes += op.reordered;
    }

    return resultado;
}

// ============================================================
// FASE 4: PROCESAMIENTO PARALELO CON OpenMP
// ============================================================

/**
 * Versión paralelizada con OpenMP usando reducción manual por hilo.
 *
 * Estrategia para evitar condiciones de carrera SIN locks globales:
 * ─────────────────────────────────────────────────────────────────
 * 1. Cada hilo mantiene su PROPIA copia local del mapa de métricas
 *    (thread-local storage mediante vector indexado por thread_id).
 * 2. El bucle paralelo escribe SOLO en el mapa local del hilo → sin contención.
 * 3. Al finalizar el bucle, una reducción secuencial fusiona todos
 *    los mapas locales en el resultado final → una sola pasada por hilo.
 *
 * Este patrón es significativamente más eficiente que usar mutex/atomic
 * porque elimina la sincronización dentro del bucle crítico.
 */
MapaMetricas procesarParalelo(const std::vector<OrdenProducto>& ordenes,
                               const MapaProductos& mapa_productos,
                               int num_hilos) {
    omp_set_num_threads(num_hilos);

    // Un mapa de métricas por cada hilo posible
    std::vector<MapaMetricas> mapas_locales(num_hilos);

    const long long n = static_cast<long long>(ordenes.size());

    // ── Zona paralela ──────────────────────────────────────────
    #pragma omp parallel
    {
        int tid = omp_get_thread_num(); // ID único del hilo actual
        MapaMetricas& local = mapas_locales[tid]; // Referencia al mapa propio

        // División del trabajo: cada hilo procesa su porción del vector
        #pragma omp for schedule(static)
        for (long long i = 0; i < n; ++i) {
            const auto& op = ordenes[static_cast<size_t>(i)];

            auto it = mapa_productos.find(op.product_id);
            if (it == mapa_productos.end()) continue;

            int dept_id = it->second;
            auto& m = local[dept_id];
            m.total_vendidos++;
            m.total_reordenes += op.reordered;
        }
    } // ── Fin zona paralela ─────────────────────────────────────

    // ── Reducción manual secuencial ────────────────────────────
    // Fusionamos todos los mapas locales en uno final
    MapaMetricas resultado;
    for (const auto& mapa : mapas_locales) {
        for (const auto& [dept_id, metrica] : mapa) {
            resultado[dept_id].total_vendidos  += metrica.total_vendidos;
            resultado[dept_id].total_reordenes += metrica.total_reordenes;
        }
    }

    return resultado;
}

// ============================================================
// UTILIDADES DE PRESENTACIÓN
// ============================================================

/**
 * Imprime la tabla de resultados con nombre de departamento,
 * totales y porcentaje de recompra, ordenada de mayor a menor recompra.
 */
void imprimirResultados(const MapaMetricas& metricas,
                         const MapaDepartamentos& deptos) {
    // Convertimos a vector para poder ordenar
    std::vector<std::tuple<std::string, long long, long long, double>> filas;
    filas.reserve(metricas.size());

    for (const auto& [dept_id, m] : metricas) {
        auto it = deptos.find(dept_id);
        std::string nombre = (it != deptos.end()) ? it->second : "Desconocido";
        double tasa = (m.total_vendidos > 0)
                      ? (100.0 * m.total_reordenes / m.total_vendidos)
                      : 0.0;
        filas.emplace_back(nombre, m.total_vendidos, m.total_reordenes, tasa);
    }

    // Ordenar por tasa de recompra descendente
    std::sort(filas.begin(), filas.end(),
              [](const auto& a, const auto& b) {
                  return std::get<3>(a) > std::get<3>(b);
              });

    // Encabezado de tabla
    std::cout << "\n";
    std::cout << std::string(75, '=') << "\n";
    std::cout << std::left
              << std::setw(22) << "Departamento"
              << std::setw(16) << "Total Vendido"
              << std::setw(18) << "Total Reordenado"
              << std::setw(14) << "% Recompra"
              << "\n";
    std::cout << std::string(75, '-') << "\n";

    for (const auto& [nombre, vendidos, reordenes, tasa] : filas) {
        std::cout << std::left
                  << std::setw(22) << nombre
                  << std::setw(16) << vendidos
                  << std::setw(18) << reordenes
                  << std::fixed << std::setprecision(2)
                  << std::setw(14) << tasa
                  << "\n";
    }
    std::cout << std::string(75, '=') << "\n";
}

/**
 * Imprime la tabla de benchmarking con tiempos y speedup.
 */
void imprimirBenchmark(double t_secuencial,
                        const std::vector<std::pair<int,double>>& resultados_paralelos) {
    std::cout << "\n";
    std::cout << std::string(55, '=') << "\n";
    std::cout << "  TABLA DE RENDIMIENTO (Benchmarking)\n";
    std::cout << std::string(55, '=') << "\n";
    std::cout << std::left
              << std::setw(18) << "Configuración"
              << std::setw(18) << "Tiempo (seg)"
              << std::setw(12) << "Speedup"
              << "\n";
    std::cout << std::string(55, '-') << "\n";

    // Fila secuencial
    std::cout << std::left
              << std::setw(18) << "Secuencial (1 hilo)"
              << std::setw(18) << std::fixed << std::setprecision(4) << t_secuencial
              << std::setw(12) << "1.00x"
              << "\n";

    // Filas paralelas
    for (const auto& [hilos, tiempo] : resultados_paralelos) {
        double speedup = t_secuencial / tiempo;
        std::string config = std::to_string(hilos) + " hilos";

        std::cout << std::left
                  << std::setw(18) << config
                  << std::setw(18) << std::fixed << std::setprecision(4) << tiempo
                  << std::fixed << std::setprecision(2) << speedup << "x"
                  << "\n";
    }

    std::cout << std::string(55, '=') << "\n";
}

// ============================================================
// MAIN
// ============================================================

int main() {
    // ── Rutas de archivos ──────────────────────────────────────
    const std::string RUTA_DEPTOS    = "../dataset/departments.csv";
    const std::string RUTA_PRODUCTOS = "../dataset/products.csv";
    const std::string RUTA_ORDENES   = "../dataset/order_products__prior.csv";

    std::cout << "============================================\n";
    std::cout << "  Análisis de Reorder Rate - Instacart\n";
    std::cout << "============================================\n\n";

    // ── FASE 1: Carga relacional ───────────────────────────────
    std::cout << "--- Cargando datos relacionales ---\n";
    MapaDepartamentos deptos    = cargarDepartamentos(RUTA_DEPTOS);
    MapaProductos     productos = cargarProductos(RUTA_PRODUCTOS);

    if (deptos.empty() || productos.empty()) {
        std::cerr << "[FATAL] No se pudieron cargar los datos maestros.\n";
        return 1;
    }

    // ── FASE 2: Lectura masiva ─────────────────────────────────
    std::cout << "\n--- Cargando dataset masivo ---\n";
    double t_carga_inicio = omp_get_wtime();
    std::vector<OrdenProducto> ordenes = cargarOrdenes(RUTA_ORDENES);
    double t_carga_fin = omp_get_wtime();

    if (ordenes.empty()) {
        std::cerr << "[FATAL] No se pudieron cargar las órdenes.\n";
        return 1;
    }
    std::cout << "[OK] Tiempo de carga del dataset: "
              << std::fixed << std::setprecision(3)
              << (t_carga_fin - t_carga_inicio) << " seg\n";

    // ── FASE 3: Procesamiento secuencial (baseline) ────────────
    std::cout << "\n--- Procesamiento SECUENCIAL ---\n";
    double t_seq_inicio = omp_get_wtime();
    MapaMetricas resultado_seq = procesarSecuencial(ordenes, productos);
    double t_seq_fin = omp_get_wtime();
    double t_secuencial = t_seq_fin - t_seq_inicio;

    std::cout << "[OK] Tiempo secuencial: "
              << std::fixed << std::setprecision(4)
              << t_secuencial << " seg\n";

    // ── Mostrar resultados del secuencial ──────────────────────
    std::cout << "\n=== Resultados (calculados de forma secuencial) ===";
    imprimirResultados(resultado_seq, deptos);

    // ── FASE 4: Procesamiento paralelo con varios hilos ────────
    std::cout << "\n--- Procesamiento PARALELO (OpenMP) ---\n";
    std::vector<int> configuraciones_hilos = {2, 4, 8};
    std::vector<std::pair<int, double>> benchmark_paralelo;

    for (int num_hilos : configuraciones_hilos) {
        // sin restricción de omp_get_max_threads()
        // forzamos directamente el uso de num_hilos
        
        std::cout << "  Ejecutando con " << num_hilos << " hilo(s)...";

        double t_par_inicio = omp_get_wtime();
        // pasamos num_hilos directamente a la función
        MapaMetricas resultado_par = procesarParalelo(ordenes, productos, num_hilos);
        double t_par_fin = omp_get_wtime();
        double t_paralelo = t_par_fin - t_par_inicio;

        std::cout << " Listo en " << std::fixed << std::setprecision(4)
                  << t_paralelo << " seg"
                  << " (Speedup: " << std::fixed << std::setprecision(2)
                  << (t_secuencial / t_paralelo) << "x)\n";

        benchmark_paralelo.emplace_back(num_hilos, t_paralelo);

        // Verificación de consistencia: comparamos totales globales
        long long total_seq = 0, total_par = 0;
        for (const auto& [id, m] : resultado_seq) total_seq += m.total_vendidos;
        for (const auto& [id, m] : resultado_par) total_par += m.total_vendidos;

        if (total_seq != total_par) {
            std::cerr << "  [ADVERTENCIA] Resultados inconsistentes con "
                      << num_hilos << " hilos! "
                      << "seq=" << total_seq << " par=" << total_par << "\n";
        }
    }

    // ── FASE 5: Tabla de benchmarking final ────────────────────
    imprimirBenchmark(t_secuencial, benchmark_paralelo);

    // ── Información del entorno OpenMP ─────────────────────────
    std::cout << "\n[INFO] Procesadores disponibles: "
              << omp_get_num_procs() << "\n";
    std::cout << "[INFO] Hilos máximos del sistema: "
              << omp_get_max_threads() << "\n";

    return 0;
}