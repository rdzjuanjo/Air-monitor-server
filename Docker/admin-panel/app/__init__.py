import os

from flask import Flask
from flask_login import LoginManager

from .auth import User, load_users


def create_app() -> Flask:
    app = Flask(__name__)

    app.config["SECRET_KEY"] = os.environ.get("SECRET_KEY", "dev")
    app.config["INFLUX_URL"] = os.environ["INFLUX_URL"]
    app.config["INFLUX_TOKEN"] = os.environ["INFLUX_TOKEN"]
    app.config["INFLUX_ORG"] = os.environ["INFLUX_ORG"]
    app.config["INFLUX_BUCKET"] = os.environ["INFLUX_BUCKET"]
    app.config["MQTT_HOST"] = os.environ.get("MQTT_HOST", "mosquitto")
    app.config["MQTT_PORT"] = int(os.environ.get("MQTT_PORT", "1883"))
    app.config["MQTT_TOPIC_BASE"] = os.environ.get("MQTT_TOPIC_BASE", "monitoreo/")
    app.config["USERS_FILE"] = os.environ.get("USERS_FILE", "/app/users.yaml")
    app.config["OTA_DIR"] = os.environ.get("OTA_DIR", "/app/ota")
    app.config["OTA_BASE_URL"] = os.environ.get("OTA_BASE_URL", "")

    app.config["MAX_CONTENT_LENGTH"] = 4 * 1024 * 1024

    app.config["USERS"] = load_users(app.config["USERS_FILE"])

    login_manager = LoginManager()
    login_manager.login_view = "main.login"
    login_manager.init_app(app)

    @login_manager.user_loader
    def load_user(user_id):
        if user_id in app.config["USERS"]:
            return User(user_id)
        return None

    from .routes import bp
    app.register_blueprint(bp)

    return app
