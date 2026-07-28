// Task: adc_rango_semanal
// Corre cada 20 minutos. Por cada dispositivo calcula el min, max y rango
// (max - min) de mq135_adc en los últimos 7 días y los escribe en el
// measurement "adc_rango" del mismo bucket.
//
// Crear en InfluxDB UI → Tasks → +Create Task, pegar este script y
// reemplazar "ambiental" y "riosvivos" con los valores reales de
// INFLUX_BUCKET e INFLUX_ORG del archivo .env.

option task = {name: "adc_rango_semanal", every: 20m, offset: 2m}

minPorDevice = from(bucket: "ambiental")
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "env_monitoring" and r._field == "mq135_adc")
  |> group(columns: ["device_id"])
  |> min()

maxPorDevice = from(bucket: "ambiental")
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "env_monitoring" and r._field == "mq135_adc")
  |> group(columns: ["device_id"])
  |> max()

joined = join(tables: {mn: minPorDevice, mx: maxPorDevice}, on: ["device_id"])

filas_min = joined |> map(fn: (r) => ({_time: now(), _measurement: "adc_rango", device_id: r.device_id, _field: "adc_min",   _value: float(v: r._value_mn)}))
filas_max = joined |> map(fn: (r) => ({_time: now(), _measurement: "adc_rango", device_id: r.device_id, _field: "adc_max",   _value: float(v: r._value_mx)}))
filas_rng = joined |> map(fn: (r) => ({_time: now(), _measurement: "adc_rango", device_id: r.device_id, _field: "adc_rango", _value: float(v: r._value_mx) - float(v: r._value_mn)}))

union(tables: [filas_min, filas_max, filas_rng])
  |> to(bucket: "ambiental", org: "riosvivos")
