# mDNS para despliegue en red local (`NGINX_PROFILE=lan`)

Cuando el dominio es algo como `dash.local`, los navegadores y los ESP32
resuelven `dash.local`, `admin.dash.local`, `mqtt.dash.local`,
`ota.dash.local`, etc. via mDNS (Avahi). Estos nombres con varios niveles
(`sub.dash.local`) no los publica Avahi por defecto — solo el hostname del
equipo (`dash.local`). Por eso necesitamos publicarlos explicitamente.

Este directorio define un servicio systemd que publica esos alias mientras
el Raspberry Pi este vivo. **Solo aplica al perfil `lan`**: si
`Docker/.env` tiene `NGINX_PROFILE=vps`, el script no publica nada (ese
dominio se resuelve por DNS real).

## Instalacion (una sola vez en el Raspberry Pi)

```bash
sudo cp Docker/mdns/dash-mdns.sh /usr/local/bin/dash-mdns.sh
sudo chmod +x /usr/local/bin/dash-mdns.sh
sudo cp Docker/mdns/dash-mdns.service /etc/systemd/system/dash-mdns.service
sudo systemctl daemon-reload
sudo systemctl enable --now dash-mdns
```

## Que publica

Lee `DOMAIN` y `NGINX_PROFILE` de `Docker/.env` y, si `NGINX_PROFILE=lan`,
publica via `avahi-publish -a -R` la IP del Pi para:

- `${DOMAIN}` (p. ej. `dash.local`)
- `grafana.${DOMAIN}`
- `admin.${DOMAIN}`
- `influx.${DOMAIN}`
- `mqtt.${DOMAIN}`
- `ota.${DOMAIN}`

Esta lista debe coincidir con los `server_name` definidos en
`Docker/nginx/templates/lan/*.conf.template`. Si se agrega un nuevo
subdominio ahi, agregarlo tambien a `SUBDOMAINS` en `dash-mdns.sh`.

## Verificar

```bash
avahi-resolve -n ota.dash.local
systemctl status dash-mdns
```

## Notas

- En el perfil `vps` (sensio.mx) este servicio no es necesario; el dominio
  se resuelve por DNS publico/Let's Encrypt y el script no hace nada.
- Si la IP del Pi cambia, basta con reiniciar el servicio
  (`sudo systemctl restart dash-mdns`) ya que la IP se detecta en cada
  arranque.
