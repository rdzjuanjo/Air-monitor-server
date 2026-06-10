import yaml
from flask_login import UserMixin
from werkzeug.security import check_password_hash


class User(UserMixin):
    def __init__(self, username: str):
        self.id = username


def load_users(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f) or {}
    return data.get("users", {})


def verify_password(users: dict, username: str, password: str) -> bool:
    password_hash = users.get(username)
    if not password_hash:
        return False
    return check_password_hash(password_hash, password)
