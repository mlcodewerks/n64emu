"""Keep standalone timing tests tied to the production RDP model."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent

def model(path):
    # The standalone distribution adds its license annotation to this comment.
    return path.read_text().replace(
        'requirements. \n * License: WTFPL */', 'requirements. */')

if model(root / 'src/device/rcp/rdp/rdp_timing.h') != model(root / 'rdp_timing/rdp_timing.h'):
    raise SystemExit('RDP timing copies differ: standalone tests would exercise a different model.')
print('RDP standalone tests use the production timing model.')
