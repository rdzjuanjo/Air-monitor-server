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
