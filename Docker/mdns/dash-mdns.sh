#!/bin/sh
# Publica alias mDNS (*.local) para el dominio y subdominios del panel
# cuando el despliegue es en red local (NGINX_PROFILE=lan).
#
# En el perfil "vps" (sensio.mx) el dominio se resuelve por DNS real,
# por lo que este script no hace nada en ese caso.

set -e

REPO_DIR="${REPO_DIR:-/home/juanjo/env-dashboard/Air-monitor-server}"
ENV_FILE="$REPO_DIR/Docker/.env"

if [ ! -f "$ENV_FILE" ]; then
    echo "dash-mdns: no se encontro $ENV_FILE, no se publica nada." >&2
    exit 0
fi

NGINX_PROFILE=$(grep -E '^NGINX_PROFILE=' "$ENV_FILE" | cut -d= -f2- | tr -d '[:space:]')
DOMAIN=$(grep -E '^DOMAIN=' "$ENV_FILE" | cut -d= -f2- | tr -d '[:space:]')

if [ "$NGINX_PROFILE" != "lan" ]; then
    echo "dash-mdns: NGINX_PROFILE='$NGINX_PROFILE' (no es 'lan'), no se publica nada." >&2
    exit 0
fi

if [ -z "$DOMAIN" ]; then
    echo "dash-mdns: DOMAIN no esta definido en $ENV_FILE." >&2
    exit 0
fi

IP="${MDNS_IP:-$(hostname -I | awk '{print $1}')}"

# Subdominios servidos por nginx en el perfil lan (ver Docker/nginx/templates/lan/*.conf.template)
SUBDOMAINS="grafana admin influx mqtt ota"

avahi-publish -a -R "$DOMAIN" "$IP" &
for sub in $SUBDOMAINS; do
    avahi-publish -a -R "$sub.$DOMAIN" "$IP" &
done

wait
