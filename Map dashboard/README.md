# Dashboard dinamico de estaciones ambientales (24h)

MVP en Python para visualizar la evolucion espacial de mediciones ambientales en las ultimas 24 horas, en pasos de 20 minutos, usando InfluxDB 2.x y Streamlit + Plotly.

## Esquema de datos esperado
Cada registro debe incluir:
- `timestamp`
- `station_id`
- `lat`
- `lon`
- `medicion`

## Variables de entorno
Copia `.env.example` y exporta variables:

- `INFLUX_URL`
- `INFLUX_TOKEN`
- `INFLUX_ORG`
- `INFLUX_BUCKET`
- `INFLUX_MEASUREMENT`
- `INFLUX_STATION_TAG`
- `INFLUX_VALUE_FIELD`
- `INFLUX_LAT_FIELD`
- `INFLUX_LON_FIELD`
- `DASHBOARD_WINDOW_HOURS`
- `DASHBOARD_STEP_MINUTES`

## Instalar dependencias
```bash
pip install -r requirements.txt
```

## Ejecutar
```bash
streamlit run app.py
```

## Que hace el MVP
- Consulta dinamicamente InfluxDB 2.x via Flux.
- Toma ventana movil de 24h.
- Agrega por intervalos de 20 min.
- Agrupa por estacion por intervalo (media de medicion).
- Muestra animacion temporal en mapa por puntos de estacion.
- Reporta cobertura media y chequea consistencia lat/lon por estacion.

## Notas de rendimiento
- Si hay mucho volumen, filtra estaciones en la barra lateral.
- Si el esquema real usa otros nombres, ajusta variables `INFLUX_*_FIELD`.
