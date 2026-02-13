"""Dynamic UI generation from JSON Schema."""

from __future__ import annotations

import json
import logging
from typing import Any, Optional

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QFormLayout,
    QLineEdit, QSpinBox, QDoubleSpinBox, QCheckBox,
    QComboBox, QTextEdit, QPushButton, QLabel,
    QGroupBox, QScrollArea
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
    if '$ref' in schema:
        ref = schema['$ref']

        # Handle #/$defs/... references
        if ref.startswith('#/$defs/'):
            def_name = ref.replace('#/$defs/', '')
            defs = root_schema.get('$defs', {})
            if def_name in defs:
                resolved = defs[def_name].copy()
                # Recursively resolve in case the definition has more refs
                return resolve_schema_refs(resolved, root_schema)
            else:
                logger.warning(f"Definition not found: {def_name}")
                return schema

        # Handle #/definitions/... references (older style)
        elif ref.startswith('#/definitions/'):
            def_name = ref.replace('#/definitions/', '')
            defs = root_schema.get('definitions', {})
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
            result[key] = [resolve_schema_refs(item, root_schema) if isinstance(item, dict) else item for item in value]
        else:
            result[key] = value

    return result


class SchemaWidget(QWidget):
    """Widget that generates UI from JSON Schema."""

    value_changed = Signal(dict)

    def __init__(self, schema: dict, parent: Optional[QWidget] = None):
        """
        Initialize schema-based widget.

        Args:
            schema: JSON Schema dictionary
            parent: Parent widget
        """
        super().__init__(parent)
        # Resolve any $ref references in the schema
        self.schema = resolve_schema_refs(schema)
        self.fields = {}
        self._build_ui()

    def _format_property_name(self, name: str) -> str:
        """
        Format property name for display.

        Args:
            name: Property name (e.g., "is_debug", "n_trials")

        Returns:
            Formatted label
        """
        # Replace underscores with spaces and capitalize
        formatted = name.replace('_', ' ').title()

        # Handle common prefixes
        if formatted.startswith('Is '):
            formatted = formatted[3:] + '?'
        elif formatted.startswith('Has '):
            formatted = formatted[4:] + '?'
        elif formatted.startswith('N '):
            formatted = 'Number of ' + formatted[2:]

        return formatted

    def _build_ui(self):
        """Build UI from schema."""
        layout = QVBoxLayout(self)

        # Create scroll area for potentially long forms
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)

        # Container widget for the form
        container = QWidget()
        form_layout = QFormLayout(container)
        form_layout.setFieldGrowthPolicy(QFormLayout.ExpandingFieldsGrow)

        # Parse schema and create widgets
        schema_type = self.schema.get('type', 'object')
        # Handle case where type is an array (e.g., ["object"])
        if isinstance(schema_type, list):
            schema_type = schema_type[0] if len(schema_type) > 0 else 'object'
        logger.debug(f"Schema type: {schema_type}")

        if schema_type == 'object':
            properties = self.schema.get('properties', {})
            required = self.schema.get('required', [])

            for prop_name, prop_schema in properties.items():
                widget = self._create_widget_for_property(prop_name, prop_schema, prop_name in required)
                if widget:
                    # Create human-readable label
                    label = self._format_property_name(prop_name)
                    if prop_name in required:
                        label += " *"

                    # Add description if available
                    description = prop_schema.get('description', '')
                    if description:
                        label_widget = QLabel(f"{label}\n<small>{description}</small>")
                        label_widget.setTextFormat(Qt.RichText)
                        label_widget.setWordWrap(True)
                        form_layout.addRow(label_widget, widget)
                    else:
                        form_layout.addRow(label, widget)

                    self.fields[prop_name] = widget
                else:
                    logger.warning(f"No widget created for property: {prop_name}")

            logger.info(f"Created {len(self.fields)} field widgets")
        else:
            logger.warning(f"Unsupported schema type: {schema_type}")

        scroll.setWidget(container)
        layout.addWidget(scroll)
        logger.debug("SchemaWidget UI build complete")

    def _create_widget_for_property(
        self,
        name: str,
        prop_schema: dict,
        required: bool
    ) -> Optional[QWidget]:
        """
        Create appropriate widget based on property schema.

        Args:
            name: Property name
            prop_schema: Property schema
            required: Whether field is required

        Returns:
            Widget for the property
        """
        prop_type = prop_schema.get('type', 'string')
        # Handle case where type is an array (e.g., ["string", "null"])
        if isinstance(prop_type, list):
            # Use the first non-null type
            prop_type = next((t for t in prop_type if t != 'null'), prop_type[0])

        enum_values = prop_schema.get('enum', None)

        # Handle enum types with combo box
        if enum_values:
            widget = QComboBox()
            widget.addItems([str(v) for v in enum_values])
            default = prop_schema.get('default')
            if default and str(default) in [str(v) for v in enum_values]:
                widget.setCurrentText(str(default))
            return widget

        # Handle different primitive types
        if prop_type == 'string':
            # Check for multiline hint
            if prop_schema.get('format') == 'textarea' or prop_schema.get('multiline'):
                widget = QTextEdit()
                widget.setMaximumHeight(100)
                default = prop_schema.get('default', '')
                widget.setPlainText(str(default))
            else:
                widget = QLineEdit()
                default = prop_schema.get('default', '')
                widget.setText(str(default))
                # Set placeholder from description or example
                placeholder = prop_schema.get('examples', [None])[0] or prop_schema.get('description', '')
                if placeholder:
                    widget.setPlaceholderText(str(placeholder))
            return widget

        elif prop_type == 'integer':
            widget = QSpinBox()
            minimum = prop_schema.get('minimum', -2147483648)
            maximum = prop_schema.get('maximum', 2147483647)

            # Handle large integer ranges (use QDoubleSpinBox for unsigned int ranges)
            if maximum > 2147483647 or minimum < -2147483648:
                widget = QDoubleSpinBox()
                widget.setDecimals(0)
                widget.setSingleStep(1.0)  # Step by 1 for integer behavior
                widget.setMinimum(max(minimum, -1e15))
                widget.setMaximum(min(maximum, 1e15))
                # Store metadata to indicate this is an integer field
                widget.setProperty("integer_field", True)
            else:
                widget.setMinimum(minimum)
                widget.setMaximum(maximum)

            widget.setValue(prop_schema.get('default', max(0, minimum)))
            return widget

        elif prop_type == 'number':
            widget = QDoubleSpinBox()
            widget.setMinimum(prop_schema.get('minimum', -1e308))
            widget.setMaximum(prop_schema.get('maximum', 1e308))
            widget.setValue(prop_schema.get('default', 0.0))
            widget.setDecimals(prop_schema.get('decimals', 2))
            return widget

        elif prop_type == 'boolean':
            widget = QCheckBox()
            widget.setChecked(prop_schema.get('default', False))
            return widget

        elif prop_type == 'array':
            # Simple array handling - use text edit with JSON
            widget = QTextEdit()
            widget.setMaximumHeight(100)
            default = prop_schema.get('default', [])
            widget.setPlainText(json.dumps(default, indent=2))
            widget.setPlaceholderText("Enter JSON array")
            return widget

        elif prop_type == 'object':
            # Nested object - create a group box
            group = QGroupBox(self._format_property_name(name))
            group_layout = QFormLayout(group)

            nested_properties = prop_schema.get('properties', {})
            nested_required = prop_schema.get('required', [])

            for nested_name, nested_schema in nested_properties.items():
                nested_widget = self._create_widget_for_property(
                    nested_name,
                    nested_schema,
                    nested_name in nested_required
                )
                if nested_widget:
                    label = self._format_property_name(nested_name)
                    if nested_name in nested_required:
                        label += " *"
                    group_layout.addRow(label, nested_widget)

                    # Store with namespaced key
                    self.fields[f"{name}.{nested_name}"] = nested_widget

            return group

        # Fallback to string input
        widget = QLineEdit()
        widget.setPlaceholderText(f"Enter {prop_type}")
        return widget

    def get_values(self) -> dict:
        """
        Get current values from all fields.

        Returns:
            Dictionary of field values
        """
        values = {}

        for field_name, widget in self.fields.items():
            # Handle nested fields
            if '.' in field_name:
                parts = field_name.split('.')
                current = values
                for part in parts[:-1]:
                    if part not in current:
                        current[part] = {}
                    current = current[part]
                field_key = parts[-1]
                target = current
            else:
                field_key = field_name
                target = values

            # Extract value based on widget type
            if isinstance(widget, QLineEdit):
                target[field_key] = widget.text()
            elif isinstance(widget, QTextEdit):
                text = widget.toPlainText()
                # Try to parse as JSON for arrays/objects
                try:
                    target[field_key] = json.loads(text)
                except json.JSONDecodeError:
                    target[field_key] = text
            elif isinstance(widget, QSpinBox):
                target[field_key] = widget.value()
            elif isinstance(widget, QDoubleSpinBox):
                value = widget.value()
                # If this is an integer field (large range), convert to int
                if widget.property("integer_field"):
                    target[field_key] = int(value)
                else:
                    target[field_key] = value
            elif isinstance(widget, QCheckBox):
                target[field_key] = widget.isChecked()
            elif isinstance(widget, QComboBox):
                target[field_key] = widget.currentText()

        return values

    def set_values(self, values: dict):
        """
        Set field values from dictionary.

        Args:
            values: Dictionary of field values (may be nested)
        """
        flat = self._flatten_values(values)
        for field_name, value in flat.items():
            if field_name not in self.fields:
                continue

            widget = self.fields[field_name]

            if isinstance(widget, QLineEdit):
                widget.setText(str(value))
            elif isinstance(widget, QTextEdit):
                if isinstance(value, (dict, list)):
                    widget.setPlainText(json.dumps(value, indent=2))
                else:
                    widget.setPlainText(str(value))
            elif isinstance(widget, QSpinBox):
                widget.setValue(int(value))
            elif isinstance(widget, QDoubleSpinBox):
                # Convert to int if this is an integer field, otherwise float
                if widget.property("integer_field"):
                    widget.setValue(int(value))
                else:
                    widget.setValue(float(value))
            elif isinstance(widget, QCheckBox):
                widget.setChecked(bool(value))
            elif isinstance(widget, QComboBox):
                widget.setCurrentText(str(value))

    @staticmethod
    def _flatten_values(values: dict, prefix: str = "") -> dict:
        """Flatten nested dicts into dot-notation keys to match field storage."""
        flat = {}
        for key, value in values.items():
            full_key = f"{prefix}.{key}" if prefix else key
            if isinstance(value, dict):
                flat.update(SchemaWidget._flatten_values(value, full_key))
            else:
                flat[full_key] = value
        return flat


class PluginConfigWidget(QWidget):
    """Widget for configuring a plugin using its schema."""

    config_changed = Signal(dict)

    def __init__(self, plugin_name: str, schema_str: str, parent: Optional[QWidget] = None, default_config: str = ""):
        """
        Initialize plugin configuration widget.

        Args:
            plugin_name: Name of the plugin
            schema_str: JSON schema string
            parent: Parent widget
            default_config: JSON string of default configuration values
        """
        super().__init__(parent)
        self.plugin_name = plugin_name
        self.schema_widget = None
        self.default_config = default_config

        logger.info(f"Initializing PluginConfigWidget for {plugin_name}")

        try:
            self.schema = json.loads(schema_str) if schema_str else {}
        except json.JSONDecodeError as e:
            logger.error(f"Failed to parse schema for {plugin_name}: {e}")
            logger.error(f"Schema string was: {schema_str}")
            self.schema = {}

        self._build_ui()
        self._apply_default_config()
        logger.info(f"PluginConfigWidget built for {plugin_name}")

    def _build_ui(self):
        """Build the configuration UI."""
        layout = QVBoxLayout(self)

        # Plugin title
        title = QLabel(f"<h2>{self.plugin_name} Configuration</h2>")
        title.setTextFormat(Qt.RichText)
        layout.addWidget(title)

        # Schema description if available
        if 'description' in self.schema:
            desc = QLabel(f"<i>{self.schema['description']}</i>")
            desc.setTextFormat(Qt.RichText)
            desc.setWordWrap(True)
            layout.addWidget(desc)

        # Create schema widget
        if self.schema:
            logger.info(f"Creating SchemaWidget for {self.plugin_name}")
            try:
                self.schema_widget = SchemaWidget(self.schema)
                layout.addWidget(self.schema_widget, 1)
                logger.info(f"SchemaWidget added to layout for {self.plugin_name}")

                # Buttons
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
            logger.warning(f"No schema available for {self.plugin_name}")
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
                logger.info(f"Applied default configuration for {self.plugin_name}")
        except json.JSONDecodeError as e:
            logger.warning(f"Failed to parse default config for {self.plugin_name}: {e}")

    def _on_apply(self):
        """Handle apply button click."""
        if self.schema_widget:
            values = self.schema_widget.get_values()
            logger.info(f"Applying configuration for {self.plugin_name}: {values}")

            # Validate against schema if jsonschema is available
            if JSONSCHEMA_AVAILABLE:
                try:
                    validate(instance=values, schema=self.schema)
                    logger.info(f"Configuration validated successfully for {self.plugin_name}")
                except ValidationError as e:
                    logger.error(f"Validation error for {self.plugin_name}: {e.message}")
                    from PySide6.QtWidgets import QMessageBox
                    QMessageBox.warning(
                        self,
                        "Validation Error",
                        f"Configuration validation failed:\n\n{e.message}"
                    )
                    return

            self.config_changed.emit(values)

    def _on_reset(self):
        """Handle reset button click."""
        if self.schema_widget:
            # Extract defaults from schema
            defaults = {}
            properties = self.schema.get('properties', {})
            for prop_name, prop_schema in properties.items():
                if 'default' in prop_schema:
                    defaults[prop_name] = prop_schema['default']

            self.schema_widget.set_values(defaults)
            logger.info(f"Reset {self.plugin_name} configuration to defaults")

    def get_configuration(self) -> dict:
        """Get current configuration values."""
        if self.schema_widget:
            return self.schema_widget.get_values()
        return {}
