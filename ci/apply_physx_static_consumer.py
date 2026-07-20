from pathlib import Path
import subprocess

path = Path("algorithmLib/algorithmSrc/norm/physics_sdk_compat_demo/physx_compatibility_demo_plugin.cpp")
text = path.read_text(encoding="utf-8")
old = """    ground_ = CreateGround();
    box_a_ = CreateBox(PxVec3(-1.5f, 3.5f, 0.0f), PxVec3(0.75f, 0.75f, 0.75f));
    box_b_ = CreateBox(PxVec3(1.5f, 5.5f, 0.0f), PxVec3(0.65f, 0.65f, 0.65f));
    ResetBodies();
    scene_->addActor(*ground_);
    trace_ << \"ground.added\\n\" << std::flush;
    scene_->addActor(*box_a_);
    trace_ << \"box_a.added\\n\" << std::flush;
    scene_->addActor(*box_b_);
    trace_ << \"box_b.added\\n\" << std::flush;
"""
new = """    ground_ = CreateGround();
    box_a_ = CreateBox(PxVec3(-1.5f, 3.5f, 0.0f), PxVec3(0.75f, 0.75f, 0.75f));
    box_b_ = CreateBox(PxVec3(1.5f, 5.5f, 0.0f), PxVec3(0.65f, 0.65f, 0.65f));
    scene_->addActor(*ground_);
    trace_ << \"ground.added\\n\" << std::flush;
    scene_->addActor(*box_a_);
    trace_ << \"box_a.added\\n\" << std::flush;
    scene_->addActor(*box_b_);
    trace_ << \"box_b.added\\n\" << std::flush;
    ResetBodies();
"""

if old in text:
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
elif new not in text:
    raise RuntimeError("Expected PhysX initialization block was not found.")

subprocess.run(["git", "config", "user.name", "github-actions[bot]"], check=True)
subprocess.run(
    ["git", "config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"],
    check=True,
)
subprocess.run(["git", "add", str(path)], check=True)
if subprocess.run(["git", "diff", "--cached", "--quiet"]).returncode != 0:
    subprocess.run(
        ["git", "commit", "-m", "fix: add PhysX actors before resetting bodies"],
        check=True,
    )
    subprocess.run(
        ["git", "push", "origin", "HEAD:ci/linux-build-probe"],
        check=True,
    )
