"""Page 1: Basic protocol information."""

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QFormLayout, QLineEdit, QTextEdit, QLabel
)
from PySide6.QtCore import Qt, Signal


class BasicInfoPage(QWidget):
    """Page 1: Protocol name, participant ID, and notes."""

    content_changed = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._init_ui()
        self._connect_signals()

    def _connect_signals(self):
        """Connect input field signals to emit content_changed."""
        self.name_input.textChanged.connect(lambda: self.content_changed.emit())
        self.participant_id_input.textChanged.connect(lambda: self.content_changed.emit())
        self.notes_input.textChanged.connect(lambda: self.content_changed.emit())

    def _init_ui(self):
        """Initialize the UI."""
        layout = QVBoxLayout(self)

        # Page title
        title = QLabel("<h2>Protocol Information</h2>")
        title.setTextFormat(Qt.RichText)
        layout.addWidget(title)

        # Description
        description = QLabel(
            "Provide basic information about the experiment protocol. "
            "Fields marked with * are required."
        )
        description.setWordWrap(True)
        layout.addWidget(description)

        # Form layout
        form_layout = QFormLayout()
        form_layout.setFieldGrowthPolicy(QFormLayout.ExpandingFieldsGrow)

        # Name field (required)
        self.name_input = QLineEdit()
        self.name_input.setPlaceholderText("Enter protocol name")
        form_layout.addRow("Protocol Name *", self.name_input)

        # Participant ID field (required)
        self.participant_id_input = QLineEdit()
        self.participant_id_input.setPlaceholderText("Enter participant ID")
        form_layout.addRow("Participant ID *", self.participant_id_input)

        # Notes field (optional)
        self.notes_input = QTextEdit()
        self.notes_input.setPlaceholderText("Enter any notes or description (optional)")
        self.notes_input.setMaximumHeight(150)
        form_layout.addRow("Notes", self.notes_input)

        layout.addLayout(form_layout)
        layout.addStretch()

    def get_data(self) -> dict:
        """
        Get the data from the page.

        Returns:
            Dictionary with keys: name, participant_id, notes
        """
        return {
            'name': self.name_input.text().strip(),
            'participant_id': self.participant_id_input.text().strip(),
            'notes': self.notes_input.toPlainText().strip()
        }

    def is_valid(self) -> tuple[bool, str]:
        """
        Validate the page data.

        Returns:
            Tuple of (is_valid, error_message)
        """
        data = self.get_data()

        if not data['name']:
            return False, "Protocol name is required"

        if not data['participant_id']:
            return False, "Participant ID is required"

        return True, ""

    def set_data(self, data: dict):
        """
        Set the page data from a dictionary.

        Args:
            data: Dictionary with keys: name, participant_id, notes
        """
        self.name_input.setText(data.get('name', ''))
        self.participant_id_input.setText(data.get('participant_id', ''))
        self.notes_input.setPlainText(data.get('notes', ''))
