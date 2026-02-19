"""Dialog for scaffolding a new Reyer render plugin."""

from pathlib import Path

from PySide6.QtCore import Qt, QRegularExpression
from PySide6.QtGui import QRegularExpressionValidator
from PySide6.QtWidgets import (
    QDialog,
    QDialogButtonBox,
    QFileDialog,
    QFormLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from src.plugin_scaffold import generate_plugin, to_pascal_case


class NewTaskDialog(QDialog):
    """Dialog for scaffolding a new Reyer render plugin."""

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.setWindowTitle("New Task")
        self.setMinimumWidth(460)
        self._generated_dir: Path | None = None
        self._class_manually_edited = False
        self._init_ui()

    def _init_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setSpacing(12)

        title = QLabel("New Task Plugin")
        title.setStyleSheet("font-size: 16px; font-weight: bold;")
        layout.addWidget(title)

        form = QFormLayout()
        form.setLabelAlignment(Qt.AlignRight)
        form.setFieldGrowthPolicy(QFormLayout.ExpandingFieldsGrow)

        self._name_edit = QLineEdit()
        self._name_edit.setPlaceholderText("e.g. Smooth Pursuit")
        self._name_edit.textChanged.connect(self._on_name_changed)
        form.addRow("Task name *", self._name_edit)

        self._class_edit = QLineEdit()
        self._class_edit.setPlaceholderText("auto-generated")
        self._class_edit.setValidator(
            QRegularExpressionValidator(QRegularExpression(r"[A-Za-z_][A-Za-z0-9_]*"))
        )
        self._class_edit.textEdited.connect(self._on_class_edited)
        form.addRow("Class name", self._class_edit)

        self._author_edit = QLineEdit()
        self._author_edit.setPlaceholderText("optional")
        form.addRow("Author", self._author_edit)

        self._desc_edit = QTextEdit()
        self._desc_edit.setPlaceholderText("optional")
        self._desc_edit.setFixedHeight(72)
        form.addRow("Description", self._desc_edit)

        layout.addLayout(form)

        hint = QLabel(
            "Files will be generated in a folder named after the task.\n"
            "You will choose where to save it."
        )
        hint.setStyleSheet("color: #888; font-size: 11px;")
        hint.setWordWrap(True)
        layout.addWidget(hint)

        buttons = QDialogButtonBox()
        self._save_btn = buttons.addButton("Save...", QDialogButtonBox.AcceptRole)
        buttons.addButton(QDialogButtonBox.Cancel)
        self._save_btn.setEnabled(False)
        buttons.accepted.connect(self._on_save)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def _on_class_edited(self, text: str) -> None:
        # If the user clears the field, resume auto-generation
        self._class_manually_edited = bool(text)

    def _on_name_changed(self, text: str) -> None:
        stripped = text.strip()
        self._save_btn.setEnabled(bool(stripped))
        if not self._class_manually_edited:
            self._class_edit.setText(to_pascal_case(stripped) if stripped else "")

    def _on_save(self) -> None:
        name = self._name_edit.text().strip()
        class_name = self._class_edit.text().strip()
        author = self._author_edit.text().strip()
        description = self._desc_edit.toPlainText().strip()

        output_dir = QFileDialog.getExistingDirectory(
            self,
            "Choose location for plugin folder",
            str(Path.home()),
        )
        if not output_dir:
            return  # user cancelled — keep dialog open

        try:
            self._generated_dir = generate_plugin(
                name, class_name, author, description, Path(output_dir)
            )
        except FileExistsError as e:
            QMessageBox.warning(self, "Folder exists", str(e))
            return
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to generate plugin:\n{e}")
            return

        self.accept()

    def generated_dir(self) -> Path | None:
        return self._generated_dir

    @staticmethod
    def create(parent: QWidget | None = None) -> Path | None:
        """Show the dialog and return the generated directory, or None if cancelled."""
        dlg = NewTaskDialog(parent)
        if dlg.exec() == QDialog.Accepted:
            return dlg.generated_dir()
        return None
