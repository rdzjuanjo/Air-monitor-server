// Task: adc_rango_semanal
// Corre cada 20 minutos. Por cada dispositivo calcula el min, max y rango
// (max - min) de mq135_adc en los últimos 7 días, y el indicador de olor
// actual (0-100%) normalizado con ese rango. Escribe todo en "adc_rango".
//
// Crear en InfluxDB UI → Tasks → +Create Task, pegar este script y
// reemplazar "ambiental" y "riosvivos" con los valores reales de
// INFLUX_BUCKET e INFLUX_ORG del archivo .env.

option task = {name: "adc_rango_semanal", every: 20m, offset: 2m}

// Min y max semanales por dispositivo. keep() elimina _time para que
// los joins sean solo por device_id y no fallen por timestamp.
minPorDevice = from(bucket: "ambiental")
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "env_monitoring" and r._field == "mq135_adc")
  |> group(columns: ["device_id"])
  |> min()
  |> rename(columns: {_value: "adc_min"})
  |> keep(columns: ["device_id", "adc_min"])

maxPorDevice = from(bucket: "ambiental")
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "env_monitoring" and r._field == "mq135_adc")
  |> group(columns: ["device_id"])
  |> max()
  |> rename(columns: {_value: "adc_max"})
  |> keep(columns: ["device_id", "adc_max"])

// Última lectura de cada dispositivo (ventana 25h para no perder sensores lentos)
latestPorDevice = from(bucket: "ambiental")
  |> range(start: -25h)
  |> filter(fn: (r) => r._measurement == "env_monitoring" and r._field == "mq135_adc")
  |> group(columns: ["device_id"])
  |> last()
  |> rename(columns: {_value: "adc_cur"})
  |> keep(columns: ["device_id", "adc_cur"])

stats = join(tables: {mn: minPorDevice, mx: maxPorDevice}, on: ["device_id"])
all   = join(tables: {stats: stats, cur: latestPorDevice}, on: ["device_id"])

filas_min  = all |> map(fn: (r) => ({_time: now(), _measurement: "adc_rango", device_id: r.device_id, _field: "adc_min",   _value: float(v: r.adc_min)}))
filas_max  = all |> map(fn: (r) => ({_time: now(), _measurement: "adc_rango", device_id: r.device_id, _field: "adc_max",   _value: float(v: r.adc_max)}))
filas_rng  = all |> map(fn: (r) => ({_time: now(), _measurement: "adc_rango", device_id: r.device_id, _field: "adc_rango", _value: float(v: r.adc_max) - float(v: r.adc_min)}))
filas_olor = all |> map(fn: (r) => ({_time: now(), _measurement: "adc_rango", device_id: r.device_id, _field: "olor_pct",
    _value: if r.adc_max > r.adc_min
      then (float(v: r.adc_cur) - float(v: r.adc_min)) / (float(v: r.adc_max) - float(v: r.adc_min)) * 100.0
      else 0.0
  }))

union(tables: [filas_min, filas_max, filas_rng, filas_olor])
  |> to(bucket: "ambiental", org: "riosvivos")
