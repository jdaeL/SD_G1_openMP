# Análisis Paralelo de Datos Masivos de Retail con OpenMP

En esta actividad se implementa una solución de computación de alto rendimiento (HPC) para procesar y analizar volúmenes masivos de datos empresariales, formando parte de una evaluación práctica sobre sistemas distribuidos y modelos de concurrencia.

## Problema
El objetivo es calcular la **Tasa de Recompra (Reorder Rate) por Departamento** utilizando el dataset masivo de [Instacart Market Basket Analysis](https://www.kaggle.com/c/instacart-market-basket-analysis/data). 

A nivel técnico, esto implica cargar catálogos relacionales en memoria y procesar concurrentemente más de **32 millones de registros de transacciones**, evitando cuellos de botella mediante paralelismo de memoria compartida.

## Stack Tecnológico
* **Lenguaje:** C++17
* **Concurrencia:** OpenMP (API de memoria compartida)
* **Entorno:** Contenedores Docker (Imagen base GCC)
* **Build System:** Make

##  Estructura del Proyecto

```text
├── dataset/                  # Archivos de datos
│   ├── departments.csv       # Catálogo de departamentos
│   ├── products.csv          # Catálogo de productos
│   └── order_products__prior.csv # +32 Millones de transacciones
└── dockerizacion/            # Código fuente e infraestructura
    ├── docker-compose.yml
    ├── Dockerfile
    ├── main.cpp              # Lógica secuencial y paralela (OpenMP)
    └── Makefile