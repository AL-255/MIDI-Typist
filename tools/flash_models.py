"""Device-independent contracts for the GUI's flashing workspace."""
from dataclasses import dataclass, field
from typing import Protocol


@dataclass(frozen=True)
class FlashAction:
    id: str
    title: str
    destination: str
    description: str


@dataclass(frozen=True)
class ConnectedDevice:
    model: str
    location: str
    token: str
    mode: str
    product: str
    serial: str
    version: str
    usb_id: str
    speed: str
    details: dict = field(default_factory=dict)


@dataclass(frozen=True)
class FirmwareImage:
    path: str
    data: bytes
    digest: str
    destination: str
    description: str


class FlashAdapter(Protocol):
    id: str
    name: str
    default_image: str
    filetypes: tuple
    safety: str
    inspection_modes: tuple[str, ...]

    def discover(self) -> list[ConnectedDevice]: ...
    def identify(self, device: ConnectedDevice, build_hint: str | None = None, control_available: bool = True) -> ConnectedDevice: ...
    def actions(self, device: ConnectedDevice) -> tuple[FlashAction, ...]: ...
    def inspect(self, token: str) -> ConnectedDevice: ...
    def load_image(self, path: str, destination: str) -> FirmwareImage: ...
    def flash(self, token: str, action: str, path: str, digest: str, progress, status): ...


def adapters():
    # Adding a board requires an adapter, not model branches in the Tk view.
    from flash_huntsman import HuntsmanAdapter
    from flash_monsgeek import MonsGeekAdapter
    return {adapter.id: adapter for adapter in (HuntsmanAdapter(), MonsGeekAdapter())}
