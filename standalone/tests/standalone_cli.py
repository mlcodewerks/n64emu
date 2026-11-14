import subprocess
import sys

app = sys.argv[1]
help_result = subprocess.run([app, '--help'], capture_output=True, text=True, timeout=10)
assert help_result.returncode == 0, help_result.stderr
assert '--rom' in help_result.stdout + help_result.stderr
for option in ['--core', '--require-hardware']:
    result = subprocess.run([app, '--hidden', option, 'unused.dll'], capture_output=True, text=True, timeout=10)
    assert result.returncode != 0, option
    assert 'Unknown argument' in result.stdout + result.stderr
print('native standalone CLI passed; external core selection rejected')
result = subprocess.run([app, '--hidden', '--context', 'vulkan'], capture_output=True, text=True, timeout=10)
assert result.returncode != 0
assert 'Only Direct3D 9' in result.stdout + result.stderr

result = subprocess.run([app, '--hidden', '--context', 'opengl'], capture_output=True, text=True, timeout=10)
assert result.returncode != 0
assert 'Only Direct3D 9' in result.stdout + result.stderr
