"""Page 2: Pipeline configuration — source and stage selection."""

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QFormLayout, QComboBox, QListWidget,
    QListWidgetItem, QLabel
)
from PySide6.QtCore import Qt, Signal

from ..messages import PluginInfo


class PipelineConfigPage(QWidget):
    """Page 2: Select pipeline source and stages."""

    content_changed = Signal()

    def __init__(
        self,
        sources: list[PluginInfo],
        stages: list[PluginInfo],
        calibrations: list[PluginInfo] | None = None,
        parent=None,
    ):
        super().__init__(parent)
        self.sources = sources
        self.stages = stages
        self.calibrations = calibrations or []
        self._init_ui()

    def _init_ui(self):
        layout = QVBoxLayout(self)

        title = QLabel("<h2>Pipeline Configuration</h2>")
        title.setTextFormat(Qt.RichText)
        layout.addWidget(title)

        description = QLabel(
            "Select the eye-tracking source and optional processing stages. "
            "The source is required. Stages are applied in the order shown."
        )
        description.setWordWrap(True)
        layout.addWidget(description)

        form = QFormLayout()
        form.setFieldGrowthPolicy(QFormLayout.ExpandingFieldsGrow)

        # Source dropdown
        self.source_combo = QComboBox()
        self.source_combo.addItem("-- Select a source --", "")
        for src in self.sources:
            self.source_combo.addItem(src.name, src.name)
        self.source_combo.currentIndexChanged.connect(
            lambda: self.content_changed.emit()
        )
        form.addRow("Source *", self.source_combo)

        # Calibration dropdown (optional)
        self.calibration_combo = QComboBox()
        self.calibration_combo.addItem("None", "")
        for cal in self.calibrations:
            self.calibration_combo.addItem(cal.name, cal.name)
        self.calibration_combo.currentIndexChanged.connect(
            lambda: self.content_changed.emit()
        )
        form.addRow("Calibration", self.calibration_combo)

        layout.addLayout(form)

        # Stages list with checkboxes
        stages_label = QLabel("<b>Processing Stages</b> (optional)")
        stages_label.setTextFormat(Qt.RichText)
        layout.addWidget(stages_label)

        self.stages_list = QListWidget()
        for stage in self.stages:
            item = QListWidgetItem(stage.name)
            item.setFlags(item.flags() | Qt.ItemIsUserCheckable)
            item.setCheckState(Qt.Unchecked)
            self.stages_list.addItem(item)
        self.stages_list.itemChanged.connect(
            lambda: self.content_changed.emit()
        )
        layout.addWidget(self.stages_list, 1)

        layout.addStretch()

    def get_data(self) -> tuple[str, str, list[str]]:
        """Return (source_name, calibration_name, [stage_names])."""
        source = self.source_combo.currentData()
        if source is None:
            source = ""

        calibration = self.calibration_combo.currentData() or ""

        stages = []
        for i in range(self.stages_list.count()):
            item = self.stages_list.item(i)
            if item.checkState() == Qt.Checked:
                stages.append(item.text())
        return source, calibration, stages

    def is_valid(self) -> tuple[bool, str]:
        source, _, _ = self.get_data()
        if not source:
            return False, "A pipeline source must be selected"
        return True, ""
