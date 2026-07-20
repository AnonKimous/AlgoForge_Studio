from pathlib import Path
import subprocess

workflow = Path(".github/workflows/linux-build-probe.yml")
text = workflow.read_text(encoding="utf-8")
old = """          git add -A -- ci/teapot_obj_parts .github/workflows/teapot-asset-diagnostic.yml
"""
new = """          git add -A -- ci .github/workflows
"""
if old in text:
    workflow.write_text(text.replace(old, new, 1), encoding="utf-8")
elif new not in text:
    raise RuntimeError("Expected temporary cleanup staging command was not found.")

subprocess.run(["git", "config", "user.name", "github-actions[bot]"], check=True)
subprocess.run(
    ["git", "config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"],
    check=True,
)
subprocess.run(["git", "add", str(workflow)], check=True)
if subprocess.run(["git", "diff", "--cached", "--quiet"]).returncode != 0:
    subprocess.run(
        ["git", "commit", "-m", "ci: make temporary cleanup idempotent"],
        check=True,
    )
    subprocess.run(
        ["git", "push", "origin", "HEAD:ci/linux-build-probe"],
        check=True,
    )
