# Flujo de trabajo: Pi (pruebas) -> VPS (produccion)

Este repo se despliega en dos lugares:

- **Raspberry Pi** (red local, `dash.local`): servidor de pruebas.
- **VPS** (`sensio.mx`): produccion.

Para evitar que un cambio sin probar llegue a produccion, se usan dos ramas:

- `main` -> lo que corre en el VPS. Solo se actualiza cuando un cambio ya
  fue probado en la Pi.
- `dev` -> rama de trabajo. Aqui se hacen los cambios y se prueban en la Pi.

## Flujo normal

1. **En la Pi**, con la rama `dev` activa:
   ```bash
   cd ~/env-dashboard/Air-monitor-server
   git switch dev
   git pull origin dev
   # editar archivos...
   git add <archivos>
   git commit -m "mensaje descriptivo"
   git push origin dev
   ```

2. **Probar en la Pi**:
   ```bash
   cd Docker
   docker compose up -d --build
   ```
   Verifica que todo funcione (paginas, dashboards, telemetria de los ESP32, etc).

3. **Si todo sale bien**, llevar el cambio a `main`:
   ```bash
   git switch main
   git pull origin main
   git merge dev
   git push origin main
   ```
   (Opcional, mas seguro: en vez de `git merge`, abrir un Pull Request en
   GitHub de `dev` -> `main` y revisar el diff antes de mezclar.)

4. **En el VPS**, aplicar el cambio ya probado:
   ```bash
   cd ~/Air-monitor-server   # o la ruta correspondiente en el VPS
   git pull origin main
   cd Docker
   docker compose up -d --build
   ```

## Monitoreo del servidor (CPU/RAM/disco)

Se agrego el servicio `telegraf-system` que escribe metricas del host
(CPU, RAM, disco, swap, red, load average) al bucket `sistema` en
InfluxDB, visibles en el dashboard "Servidor" de Grafana, con alertas
por Telegram.

Pasos unicos al desplegar esto por primera vez en un entorno (Pi o VPS):

1. **Crear el bucket `sistema` en InfluxDB** (el bucket inicial solo se
   crea una vez al inicializar InfluxDB, asi que hay que crearlo a mano):
   ```bash
   docker compose exec influxdb influx bucket create \
     --name sistema \
     --org "$INFLUX_ORG" \
     --token "$INFLUX_TOKEN"
   ```

2. **Crear un bot de Telegram para las alertas**:
   - Habla con [@BotFather](https://t.me/BotFather) en Telegram, usa
     `/newbot` y guarda el token que te da -> `TELEGRAM_BOT_TOKEN`.
   - Envia cualquier mensaje a tu bot nuevo, luego visita
     `https://api.telegram.org/bot<TOKEN>/getUpdates` y busca el campo
     `chat.id` -> `TELEGRAM_CHAT_ID`.
   - Agrega ambos valores al `.env` del entorno.

3. **Levantar el nuevo servicio**:
   ```bash
   docker compose up -d --build
   ```

## Notas

- `Docker/.env` **no se versiona** (esta en `.gitignore`). Cada entorno
  tiene el suyo:
  - Pi: `NGINX_PROFILE=lan`, `DOMAIN=dash.local`
  - VPS: `NGINX_PROFILE=vps`, `DOMAIN=sensio.mx`
- Si un cambio modifica `Docker/telegraf/telegraf.conf`, hay que reiniciar
  el servicio `telegraf` aparte (no se reconstruye con `--build`):
  ```bash
  docker compose restart telegraf
  ```
