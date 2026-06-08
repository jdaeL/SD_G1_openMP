# Análisis Paralelo de Datos Masivos de Retail con OpenMP

En esta actividad se implementa una solución de computación de alto rendimiento (HPC) para procesar y analizar volúmenes masivos de datos empresariales, formando parte de una evaluación práctica sobre **Sistemas Distribuidos** y modelos de concurrencia.

## Problema de Negocio (Caso Instacart)

El objetivo es calcular la **Tasa de Recompra (Reorder Rate) por Departamento** utilizando el dataset masivo de [Instacart Market Basket Analysis](https://www.kaggle.com/c/instacart-market-basket-analysis/data).

A nivel técnico, esto implica cargar catálogos relacionales en memoria y procesar concurrentemente más de **32,434,489 registros de transacciones**, evitando cuellos de botella y condiciones de carrera mediante paralelismo de memoria compartida.

## Stack Tecnológico

| Componente | Tecnología |
|---|---|
| Lenguaje | C++17 |
| Concurrencia | OpenMP (API de memoria compartida) |
| Entorno | Contenedores Docker (Imagen base GCC) |
| Build System | Make (`-std=c++17`, `-O3`, `-fopenmp`) |

## Estructura del Proyecto

```text
├── dataset/                        # Archivos de datos
│   ├── departments.csv             # Catálogo de departamentos
│   ├── products.csv                # Catálogo de productos
│   └── order_products__prior.csv   # +32 Millones de transacciones
└── dockerizacion/                  # Código fuente e infraestructura
    ├── docker-compose.yml
    ├── Dockerfile
    ├── main.cpp                    # Lógica secuencial y paralela (OpenMP)
    └── Makefile
```

## Arquitectura y Manejo de Concurrencia

Para evitar el uso de bloqueos o cerrojos (locks / mutex) globales que asfixiarían el rendimiento, se implementó la estrategia **Thread-Local Storage (Memoria Local por Hilo)**:

1. Dentro de la directiva `#pragma omp parallel`, cada hilo instancia de forma independiente sus propios mapas de memoria (`std::unordered_map`).
2. Cada hilo procesa su fragmento de los 32.4 millones de registros **sin contención (0% bloqueos)**.
3. Al finalizar, el hilo maestro suma secuencialmente los mapas locales, emitiendo el resultado global.

## Manual de Instalación y Ejecución

El proyecto está dockerizado para garantizar la reproducibilidad del entorno de compilación (Linux/GCC con soporte para `libgomp`).

### Requisitos Previos

- Docker y Docker Compose instalados (con WSL2 habilitado si usa Windows).
- Asignación de recursos recomendada: **8 núcleos lógicos**.

### Paso 1 — Preparar los Datos de Prueba

Por restricciones de tamaño de GitHub, el archivo de transacciones no está incluido en el repositorio.

1. Descargue los archivos desde [Kaggle — Instacart](https://www.kaggle.com/c/instacart-market-basket-analysis/data).
2. Coloque `departments.csv`, `products.csv` y `order_products__prior.csv` dentro de la carpeta `/dataset/` en la raíz del proyecto.

### Paso 2 — Despliegue de Infraestructura

Levante el contenedor aislado e ingrese al entorno Linux:

```bash
cd dockerizacion
docker compose up -d
docker compose exec openmp-node bash
```

### Paso 3 — Compilación

Dentro del contenedor, compile el proyecto utilizando el Makefile:

```bash
make
```

> Aplica los flags `-std=c++17`, `-O3` y `-fopenmp` automáticamente.

### Paso 4 — Ejecución del Análisis

```bash
# Opcional: fijar explícitamente el número de hilos disponibles
export OMP_NUM_THREADS=8

./reorder_analysis
```

## Evidencias de Ejecución y Resultados

El código procesó exitosamente más de 32 millones de registros, identificando que los departamentos de **dairy eggs (67.00%)** y **beverages (65.35%)** lideran la fidelidad de los clientes.

### Benchmarking de Escalabilidad

Se comparó una ejecución secuencial estricta contra la paralelizada con 2, 4 y 8 hilos, logrando un *Speedup* casi óptimo que demuestra la alta eficiencia del Thread-Local Storage.

```
=======================================================
  TABLA DE RENDIMIENTO (Benchmarking)
=======================================================
Configuración        Tiempo (seg)    Speedup
-------------------------------------------------------
Secuencial (1 hilo)  1.5623          1.00x
2 hilos              0.8185          1.91x
4 hilos              0.4199          3.72x
8 hilos              0.2259          6.92x
=======================================================
[INFO] Procesadores disponibles: 8
[INFO] Hilos máximos del sistema: 8
```

> El escalamiento alcanzó un pico de **6.92x** sobre un máximo teórico de 8.0x. Este comportamiento es consistente con la **Ley de Amdahl**: el overhead de inicialización de hilos y la fase secuencial de reducción final impiden alcanzar el speedup lineal ideal.