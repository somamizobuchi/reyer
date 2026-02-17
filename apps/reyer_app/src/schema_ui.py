"""Dynamic UI generation from JSON Schema."""

from __future__ import annotations

import json
import logging
from dataclasses import dataclass
from typing import Any, Callable, Optional

from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QGridLayout,
    QLineEdit,
    QDoubleSpinBox,
    QCheckBox,
    QComboBox,
    QTextEdit,
    QPushButton,
    QLabel,
    QGroupBox,
    QScrollArea,
    QSizePolicy,
)
from PySide6.QtCore import Qt, Signal

try:
    from jsonschema import validate, ValidationError, Draft7Validator

    JSONSCHEMA_AVAILABLE = True
except ImportError:
    JSONSCHEMA_AVAILABLE = False
    ValidationError = Exception

logger = logging.getLogger(__name__)


def resolve_schema_refs(schema: dict, root_schema: dict = None) -> dict:
    """
    Resolve $ref references in a JSON schema.

    Args:
        schema: Schema or sub-schema to resolve
        root_schema: Root schema containing $defs

    Returns:
        Schema with all references resolved
    """
    if root_schema is None:
        root_schema = schema

    if not isinstance(schema, dict):
        return schema

    # If this is a reference, resolve it
    if "$ref" in schema:
        ref = schema["$ref"]

        # Handle #/$defs/... references
        if ref.startswith("#/$defs/"):
            def_name = ref.replace("#/$defs/", "")
            defs = root_schema.get("$defs", {})
            if def_name in defs:
                resolved = defs[def_name].copy()
                return resolve_schema_refs(resolved, root_schema)
            else:
                logger.warning(f"Definition not found: {def_name}")
                return schema

        # Handle #/definitions/... references (older style)
        elif ref.startswith("#/definitions/"):
            def_name = ref.replace("#/definitions/", "")
            defs = root_schema.get("definitions", {})
            if def_name in defs:
                resolved = defs[def_name].copy()
                return resolve_schema_refs(resolved, root_schema)
            else:
                logger.warning(f"Definition not found: {def_name}")
                return schema

    # Recursively resolve references in nested structures
    result = {}
    for key, value in schema.items():
        if isinstance(value, dict):
            result[key] = resolve_schema_refs(value, root_schema)
        elif isinstance(value, list):
            result[key] = [
                (
                    resolve_schema_refs(item, root_schema)
                    if isinstance(item, dict)
                    else item
                )
                for item in value
            ]
        else:
            result[key] = value

    return result


@dataclass
class FieldEntry:
    """Stores a widget along with its getter and setter."""

    widget: QWidget
    getter: Callable[[], Any]
    setter: Callable[[Any], None]


def _create_enum_widget(
    enum_values: list, default: Any
) -> tuple[QWidget, Callable, Callable]:
    """Create a combo box for enum values."""
    widget = QComboBox()
    widget.addItems([str(v) for v in enum_values])
    if default is not None and str(default) in [str(v) for v in enum_values]:
        widget.setCurrentText(str(default))
    return widget, widget.currentText, lambda v: widget.setCurrentText(str(v))


def _create_string_widget(prop_schema: dict) -> tuple[QWidget, Callable, Callable]:
    """Create widget for string properties."""
    if prop_schema.get("format") == "textarea" or prop_schema.get("multiline"):
        widget = QTextEdit()
        widget.setMaximumHeight(100)
        widget.setPlainText(str(prop_schema.get("default", "")))
        return widget, widget.toPlainText, lambda v: widget.setPlainText(str(v))
    else:
        widget = QLineEdit()
        widget.setText(str(prop_schema.get("default", "")))
        placeholder = prop_schema.get("examples", [None])[0] or prop_schema.get(
            "description", ""
        )
        if placeholder:
            widget.setPlaceholderText(str(placeholder))
        return widget, widget.text, lambda v: widget.setText(str(v))


def _create_integer_widget(prop_schema: dict) -> tuple[QWidget, Callable, Callable]:
    """Create widget for integer properties."""
    widget = QDoubleSpinBox()
    widget.setDecimals(0)
    widget.setSingleStep(1.0)
    minimum = prop_schema.get("minimum", -1e15)
    maximum = prop_schema.get("maximum", 1e15)
    widget.setMinimum(minimum)
    widget.setMaximum(maximum)
    widget.setValue(prop_schema.get("default", max(0, minimum)))
    return widget, lambda: int(widget.value()), lambda v: widget.setValue(int(v))


def _create_number_widget(prop_schema: dict) -> tuple[QWidget, Callable, Callable]:
    """Create widget for number properties."""
    widget = QDoubleSpinBox()
    widget.setMinimum(prop_schema.get("minimum", -1e308))
    widget.setMaximum(prop_schema.get("maximum", 1e308))
    widget.setDecimals(prop_schema.get("decimals", 2))
    widget.setValue(prop_schema.get("default", 0.0))
    return widget, widget.value, lambda v: widget.setValue(float(v))


def _create_boolean_widget(prop_schema: dict) -> tuple[QWidget, Callable, Callable]:
    """Create widget for boolean properties."""
    widget = QCheckBox()
    widget.setChecked(prop_schema.get("default", False))
    return widget, widget.isChecked, lambda v: widget.setChecked(bool(v))


def _create_array_widget(prop_schema: dict) -> tuple[QWidget, Callable, Callable]:
    """Create widget for array properties."""
    widget = QTextEdit()
    widget.setMaximumHeight(100)
    default = prop_schema.get("default", [])
    widget.setPlainText(json.dumps(default, indent=2))
    widget.setPlaceholderText("Enter JSON array")

    def getter():
        try:
            return json.loads(widget.toPlainText())
        except json.JSONDecodeError:
            return widget.toPlainText()

    def setter(v):
        if isinstance(v, (dict, list)):
            widget.setPlainText(json.dumps(v, indent=2))
        else:
            widget.setPlainText(str(v))

    return widget, getter, setter


_TYPE_FACTORIES: dict[str, Callable[[dict], tuple[QWidget, Callable, Callable]]] = {
    "string": _create_string_widget,
    "integer": _create_integer_widget,
    "number": _create_number_widget,
    "boolean": _create_boolean_widget,
    "array": _create_array_widget,
}


class SchemaWidget(QWidget):
    """Widget that generates UI from JSON Schema."""

    value_changed = Signal(dict)

    def __init__(
        self, schema: dict, parent: Optional[QWidget] = None, _nested: bool = False
    ):
        super().__init__(parent)
        self.schema = resolve_schema_refs(schema)
        self.fields: dict[str, FieldEntry] = {}
        self._nested_widgets: dict[str, SchemaWidget] = {}
        self._is_nested = _nested
        self._build_ui()

    @staticmethod
    def _format_property_name(name: str) -> str:
        """Format property name for display."""
        formatted = name.replace("_", " ").title()
        if formatted.startswith("Is "):
            formatted = formatted[3:] + "?"
        elif formatted.startswith("Has "):
            formatted = formatted[4:] + "?"
        elif formatted.startswith("N "):
            formatted = "Number of " + formatted[2:]
        return formatted

    def _get_label(self, prop_name: str, prop_schema: dict) -> str:
        """Get display label from schema title or formatted property name."""
        return prop_schema.get("title", self._format_property_name(prop_name))

    def _build_ui(self):
        """Build UI from schema."""
        layout = QVBoxLayout(self)

        if self._is_nested:
            layout.setContentsMargins(0, 0, 0, 0)
            grid = QGridLayout()
            grid.setContentsMargins(0, 0, 0, 0)
        else:
            scroll = QScrollArea()
            scroll.setWidgetResizable(True)
            scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAsNeeded)
            scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
            container = QWidget()
            grid = QGridLayout(container)

        self._grid = grid
        grid.setColumnStretch(2, 1)

        schema_type = self.schema.get("type", "object")
        if isinstance(schema_type, list):
            schema_type = schema_type[0] if schema_type else "object"

        if schema_type == "object":
            properties = self.schema.get("properties", {})
            required = self.schema.get("required", [])
            row = 0

            for prop_name, prop_schema in properties.items():
                entry = self._create_field(prop_name, prop_schema)
                if entry is None:
                    logger.warning(f"No widget created for property: {prop_name}")
                    continue

                label_text = self._get_label(prop_name, prop_schema)
                if prop_name in required:
                    label_text += " *"

                description = prop_schema.get("description", "")
                if description:
                    label_widget = QLabel(f"{label_text}\n<small>{description}</small>")
                    label_widget.setTextFormat(Qt.RichText)
                else:
                    label_widget = QLabel(label_text)

                label_widget.setWordWrap(True)
                label_widget.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)
                label_widget.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Preferred)

                if isinstance(entry.widget, SchemaWidget):
                    # Inline nested fields into the parent grid
                    nested_grid = entry.widget._grid
                    if nested_grid and nested_grid.rowCount() > 0:
                        # First nested field shares the row with the parent label
                        first_label = nested_grid.itemAtPosition(0, 0)
                        first_field = nested_grid.itemAtPosition(0, 1)
                        if first_label and first_field:
                            fl = first_label.widget()
                            ff = first_field.widget()
                            nested_grid.removeWidget(fl)
                            nested_grid.removeWidget(ff)
                            grid.addWidget(label_widget, row, 0)
                            grid.addWidget(fl, row, 1)
                            grid.addWidget(ff, row, 2)
                            row += 1
                        # Remaining nested fields
                        remaining = []
                        for r in range(1, nested_grid.rowCount()):
                            rl = nested_grid.itemAtPosition(r, 0)
                            rf = nested_grid.itemAtPosition(r, 1)
                            if rl and rf:
                                remaining.append((rl.widget(), rf.widget()))
                        for rl_w, rf_w in remaining:
                            nested_grid.removeWidget(rl_w)
                            nested_grid.removeWidget(rf_w)
                            grid.addWidget(
                                QLabel(), row, 0
                            )  # empty spacer under parent label
                            grid.addWidget(rl_w, row, 1)
                            grid.addWidget(rf_w, row, 2)
                            row += 1
                    else:
                        grid.addWidget(label_widget, row, 0)
                        grid.addWidget(entry.widget, row, 1, 1, 2)
                        row += 1
                else:
                    grid.addWidget(label_widget, row, 0)
                    grid.addWidget(entry.widget, row, 1, 1, 2)
                    row += 1

                self.fields[prop_name] = entry

            logger.info(f"Created {len(self.fields)} field widgets")
        else:
            logger.warning(f"Unsupported schema type: {schema_type}")

        if self._is_nested:
            layout.addLayout(grid)
        else:
            scroll.setWidget(container)
            layout.addWidget(scroll)

    def _create_field(self, name: str, prop_schema: dict) -> Optional[FieldEntry]:
        """Create a FieldEntry for a property schema."""
        prop_type = prop_schema.get("type", "string")
        if isinstance(prop_type, list):
            prop_type = next((t for t in prop_type if t != "null"), prop_type[0])

        enum_values = prop_schema.get("enum", None)

        # Enum takes priority over type
        if enum_values is not None:
            widget, getter, setter = _create_enum_widget(
                enum_values, prop_schema.get("default")
            )
            widget.currentTextChanged.connect(
                lambda: self.value_changed.emit(self.get_values())
            )
            return FieldEntry(widget, getter, setter)

        # Nested object — use recursive SchemaWidget
        if prop_type == "object":
            return self._create_nested_object_field(name, prop_schema)

        # Primitive types via factory registry
        factory = _TYPE_FACTORIES.get(prop_type)
        if factory is None:
            # Fallback: string input
            widget = QLineEdit()
            widget.setPlaceholderText(f"Enter {prop_type}")
            widget.textChanged.connect(
                lambda: self.value_changed.emit(self.get_values())
            )
            return FieldEntry(widget, widget.text, lambda v: widget.setText(str(v)))

        widget, getter, setter = factory(prop_schema)

        # Connect change signals
        if isinstance(widget, QLineEdit):
            widget.textChanged.connect(
                lambda: self.value_changed.emit(self.get_values())
            )
        elif isinstance(widget, QTextEdit):
            widget.textChanged.connect(
                lambda: self.value_changed.emit(self.get_values())
            )
        elif isinstance(widget, QDoubleSpinBox):
            widget.valueChanged.connect(
                lambda: self.value_changed.emit(self.get_values())
            )
        elif isinstance(widget, QCheckBox):
            widget.stateChanged.connect(
                lambda: self.value_changed.emit(self.get_values())
            )

        return FieldEntry(widget, getter, setter)

    def _create_nested_object_field(self, name: str, prop_schema: dict) -> FieldEntry:
        """Create a recursive SchemaWidget for a nested object property."""
        nested_widget = SchemaWidget(prop_schema, _nested=True)
        nested_widget.value_changed.connect(
            lambda: self.value_changed.emit(self.get_values())
        )

        self._nested_widgets[name] = nested_widget

        return FieldEntry(
            widget=nested_widget,
            getter=nested_widget.get_values,
            setter=nested_widget.set_values,
        )

    def get_values(self) -> dict:
        """Get current values from all fields."""
        return {name: entry.getter() for name, entry in self.fields.items()}

    def set_values(self, values: dict):
        """Set field values from dictionary."""
        for name, value in values.items():
            if name in self.fields:
                self.fields[name].setter(value)


class PluginConfigWidget(QWidget):
    """Widget for configuring a plugin using its schema."""

    config_changed = Signal(dict)

    def __init__(
        self,
        plugin_name: str,
        schema_str: str,
        parent: Optional[QWidget] = None,
        default_config: str = "",
    ):
        super().__init__(parent)
        self.plugin_name = plugin_name
        self.schema_widget: Optional[SchemaWidget] = None
        self.default_config = default_config

        try:
            self.schema = json.loads(schema_str) if schema_str else {}
        except json.JSONDecodeError as e:
            logger.error(f"Failed to parse schema for {plugin_name}: {e}")
            self.schema = {}

        self._build_ui()
        self._apply_default_config()

    def _build_ui(self):
        """Build the configuration UI."""
        layout = QVBoxLayout(self)

        title = QLabel(f"<h2>{self.plugin_name}</h2>")
        title.setTextFormat(Qt.RichText)
        layout.addWidget(title)

        if "description" in self.schema:
            desc = QLabel(f"<i>{self.schema['description']}</i>")
            desc.setTextFormat(Qt.RichText)
            desc.setWordWrap(True)
            layout.addWidget(desc)

        if self.schema:
            try:
                self.schema_widget = SchemaWidget(self.schema)
                layout.addWidget(self.schema_widget, 1)

                button_layout = QHBoxLayout()
                button_layout.addStretch()

                apply_btn = QPushButton("Apply Configuration")
                apply_btn.clicked.connect(self._on_apply)
                button_layout.addWidget(apply_btn)

                reset_btn = QPushButton("Reset to Defaults")
                reset_btn.clicked.connect(self._on_reset)
                button_layout.addWidget(reset_btn)

                layout.addLayout(button_layout)
            except Exception as e:
                logger.error(f"Error creating SchemaWidget for {self.plugin_name}: {e}")
                import traceback

                logger.error(traceback.format_exc())
                error_label = QLabel(f"Error creating configuration UI: {e}")
                error_label.setWordWrap(True)
                error_label.setStyleSheet("color: red;")
                layout.addWidget(error_label)
        else:
            no_config = QLabel("No configuration schema available for this plugin.")
            no_config.setAlignment(Qt.AlignCenter)
            layout.addWidget(no_config)

    def _apply_default_config(self):
        """Apply default configuration values if available."""
        if not self.default_config or not self.schema_widget:
            return
        try:
            defaults = json.loads(self.default_config)
            if isinstance(defaults, dict):
                self.schema_widget.set_values(defaults)
        except json.JSONDecodeError as e:
            logger.warning(
                f"Failed to parse default config for {self.plugin_name}: {e}"
            )

    def _on_apply(self):
        """Handle apply button click."""
        if not self.schema_widget:
            return
        values = self.schema_widget.get_values()

        if JSONSCHEMA_AVAILABLE:
            try:
                validate(instance=values, schema=self.schema)
            except ValidationError as e:
                from PySide6.QtWidgets import QMessageBox

                QMessageBox.warning(
                    self,
                    "Validation Error",
                    f"Configuration validation failed:\n\n{e.message}",
                )
                return

        self.config_changed.emit(values)

    def _on_reset(self):
        """Handle reset button click."""
        if not self.schema_widget:
            return
        defaults = {}
        for prop_name, prop_schema in self.schema.get("properties", {}).items():
            if "default" in prop_schema:
                defaults[prop_name] = prop_schema["default"]
        self.schema_widget.set_values(defaults)

    def get_configuration(self) -> dict:
        """Get current configuration values."""
        if self.schema_widget:
            return self.schema_widget.get_values()
        return {}
