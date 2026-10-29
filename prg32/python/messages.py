"""Messages the PRG32 Python compiler writes for people, with stable keys.

A message is an English template with ``{name}`` values, for example
``"{name} is not defined. Declare it first"``. ``text("py", template, name="score")`` gives a
``Message``: the English sentence (it *is* a ``str``), plus its catalog key and values.

This is the part of PRG32-Construction-Kit's ``messages.py`` that the compiler needs. The
keys are computed exactly as in the kit (``key_for``), so a problem reported by the SDK has
the same ``message_key`` as the one the kit's IDE translates. The SDK prints the English text;
it has no translation catalogs.
"""

from __future__ import annotations

import hashlib
import re
from typing import Any

PLACEHOLDER = re.compile(r"\{([a-z_][a-z0-9_]*)\}")
_WORD = re.compile(r"[a-z0-9]+")


def key_for(area: str, template: str) -> str:
    """server.<area>.<first six words>_<4 hex digits of the whole template>."""
    words = _WORD.findall(PLACEHOLDER.sub(" ", template.lower()))[:6] or ["message"]
    digest = hashlib.sha256(template.encode("utf-8")).hexdigest()[:4]
    return f"server.{area}.{'_'.join(words)}_{digest}"


def fill(template: str, params: dict[str, Any]) -> str:
    return PLACEHOLDER.sub(lambda match: str(params[match.group(1)]) if match.group(1) in params else match.group(0),
                           template)


class Message(str):
    """An English message that also knows its catalog key and values."""

    key: str = ""
    params: dict[str, Any] = {}
    template: str = ""


def text(area: str, template: str, **params: Any) -> Message:
    message = Message(fill(template, params))
    message.key = key_for(area, template)
    message.params = {name: value if isinstance(value, (int, float, bool)) else str(value)
                      for name, value in params.items()}
    message.template = template
    return message


def of(value: Any) -> Message | None:
    """The Message in a value: itself, or the first argument of an exception that carries one."""
    if isinstance(value, Message):
        return value
    if isinstance(value, BaseException) and value.args and isinstance(value.args[0], Message):
        return value.args[0]
    return None


def fields(value: Any) -> dict[str, Any]:
    """{"message_key", "params"} for a Message (or an exception carrying one); {} otherwise."""
    message = of(value)
    return {"message_key": message.key, "params": message.params} if message is not None else {}
