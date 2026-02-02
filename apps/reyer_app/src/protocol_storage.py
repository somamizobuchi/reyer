"""Protocol storage backend for saving/loading protocols as JSON files."""

from __future__ import annotations

import msgspec
import logging
from pathlib import Path
from typing import Optional, List
from datetime import datetime
import tempfile
import os

from .messages import ProtocolRequest

logger = logging.getLogger(__name__)


class ProtocolStorage:
    """Handles saving, loading, and managing protocol files on disk."""

    def __init__(self, storage_dir: Optional[Path] = None):
        """
        Initialize protocol storage.

        Args:
            storage_dir: Directory to store protocols. Defaults to ~/.reyer/protocols/
        """
        if storage_dir is None:
            self.storage_dir = Path.home() / ".reyer" / "protocols"
        else:
            self.storage_dir = storage_dir

        self.ensure_storage_exists()

    def ensure_storage_exists(self) -> bool:
        """
        Ensure storage directory exists, creating it if necessary.

        Returns:
            True if directory exists or was created successfully, False otherwise
        """
        try:
            self.storage_dir.mkdir(parents=True, exist_ok=True)
            logger.info(f"Protocol storage directory: {self.storage_dir}")
            return True
        except (OSError, PermissionError) as e:
            logger.error(f"Failed to create storage directory {self.storage_dir}: {e}")
            return False

    def save_protocol(
        self,
        protocol: ProtocolRequest,
        auto_name: bool = True,
        custom_filename: Optional[str] = None
    ) -> tuple[bool, str, Optional[Path]]:
        """
        Save protocol to disk as JSON file.

        Args:
            protocol: ProtocolRequest object to save
            auto_name: If True, auto-generate filename with timestamp
            custom_filename: Custom filename (used if auto_name=False)

        Returns:
            Tuple of (success, message, filepath)
        """
        try:
            # Generate filename
            if auto_name:
                timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                # Sanitize protocol name for filesystem
                safe_name = "".join(c if c.isalnum() or c in ('-', '_') else '_'
                                   for c in protocol.name)
                filename = f"{safe_name}_{timestamp}.json"
            else:
                filename = custom_filename or f"{protocol.name}.json"

            filepath = self.storage_dir / filename

            # Serialize protocol to JSON
            try:
                json_data = msgspec.json.encode(protocol)
            except Exception as e:
                logger.error(f"Failed to serialize protocol: {e}")
                return False, f"Serialization error: {e}", None

            # Atomic write: write to temp file, then rename
            # This prevents corruption if write is interrupted
            temp_fd, temp_path = tempfile.mkstemp(
                dir=self.storage_dir,
                prefix=".tmp_",
                suffix=".json"
            )

            try:
                with os.fdopen(temp_fd, 'wb') as f:
                    f.write(json_data)

                # Atomic rename (overwrites if file exists)
                os.replace(temp_path, filepath)

                logger.info(f"Protocol saved: {filepath}")
                return True, f"Protocol saved to {filepath.name}", filepath

            except Exception as e:
                # Clean up temp file on error
                try:
                    os.unlink(temp_path)
                except:
                    pass
                raise e

        except (OSError, PermissionError) as e:
            logger.error(f"Failed to save protocol: {e}")
            return False, f"File I/O error: {e}", None
        except Exception as e:
            logger.error(f"Unexpected error saving protocol: {e}")
            return False, f"Error: {e}", None

    def load_protocol(self, filepath: Path) -> Optional[ProtocolRequest]:
        """
        Load protocol from JSON file.

        Args:
            filepath: Path to protocol JSON file

        Returns:
            ProtocolRequest object if successful, None otherwise
        """
        try:
            if not filepath.exists():
                logger.error(f"Protocol file not found: {filepath}")
                return None

            # Read and deserialize protocol
            with open(filepath, 'rb') as f:
                json_data = f.read()

            protocol = msgspec.json.decode(json_data, type=ProtocolRequest)
            logger.info(f"Protocol loaded: {protocol.name} from {filepath}")
            return protocol

        except msgspec.ValidationError as e:
            logger.error(f"Invalid protocol file {filepath}: {e}")
            return None
        except (OSError, PermissionError) as e:
            logger.error(f"Failed to read protocol file {filepath}: {e}")
            return None
        except Exception as e:
            logger.error(f"Unexpected error loading protocol: {e}")
            return None

    def list_protocols(self) -> List[dict]:
        """
        List all protocol files in storage directory.

        Returns:
            List of dicts with protocol metadata:
            [{name, participant_id, filepath, created_at, filename}, ...]
        """
        protocols = []

        try:
            # Find all JSON files
            json_files = sorted(
                self.storage_dir.glob("*.json"),
                key=lambda p: p.stat().st_mtime,
                reverse=True  # Most recent first
            )

            for filepath in json_files:
                try:
                    # Load protocol to get metadata
                    protocol = self.load_protocol(filepath)
                    if protocol:
                        stat = filepath.stat()
                        protocols.append({
                            'name': protocol.name,
                            'participant_id': protocol.participant_id,
                            'filepath': filepath,
                            'filename': filepath.name,
                            'created_at': datetime.fromtimestamp(stat.st_mtime)
                        })
                except Exception as e:
                    logger.warning(f"Skipping invalid protocol file {filepath}: {e}")
                    continue

        except Exception as e:
            logger.error(f"Error listing protocols: {e}")

        return protocols

    def delete_protocol(self, filepath: Path) -> bool:
        """
        Delete protocol file from disk.

        Args:
            filepath: Path to protocol file to delete

        Returns:
            True if deleted successfully, False otherwise
        """
        try:
            if not filepath.exists():
                logger.warning(f"Protocol file not found: {filepath}")
                return False

            filepath.unlink()
            logger.info(f"Protocol deleted: {filepath}")
            return True

        except (OSError, PermissionError) as e:
            logger.error(f"Failed to delete protocol {filepath}: {e}")
            return False
        except Exception as e:
            logger.error(f"Unexpected error deleting protocol: {e}")
            return False

    def get_storage_path(self) -> Path:
        """Get the storage directory path."""
        return self.storage_dir
