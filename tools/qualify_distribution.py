"""Qualify read-only CI outputs; this tool has no publication commands."""
import argparse
import json
import re
import subprocess
from pathlib import Path
from distribute import ROOT, build
from verify_distribution import verify


def run(*args):
    return subprocess.check_output(args, text=True, encoding="utf-8", errors="replace").strip()


def qualify(args):
    commit = run("git", "-C", str(ROOT), "rev-parse", "HEAD")
    if not re.fullmatch("[a-f0-9]{40}", args.commit) or commit != args.commit:
        raise ValueError("Build must use the selected full source commit")
    if run("git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=no"):
        raise ValueError("Tracked CI source is dirty")
    spec = json.loads((ROOT / "version.json").read_text())
    version = ".".join(str(spec[key]) for key in ("major","minor","patch"))
    if spec["channel"] == "rc": version += f"-rc-{spec['rc_number']}"
    if args.version and args.version != version or args.tag and args.tag != "v" + version:
        raise ValueError("Selected tag/version disagrees with version.json")
    for entry in json.loads((ROOT / "build/compile_commands.json").read_text()):
        command = entry.get("command", " ".join(entry.get("arguments", [])))
        if re.search(r"CMakeFiles[\\/](sentinel_core|sentinel_native|sentinel_native_model)\.dir", command) and "SC_NATIVE_TESTING" in command:
            raise ValueError("Testing macro enabled in production")
    bins = ROOT / "build/bin"
    exports = run("dumpbin", "/nologo", "/exports", str(bins / "msimg32.dll"))
    expected = {1:"vSetDdrawflag",2:"AlphaBlend",3:"DllInitialize",4:"GradientFill",5:"TransparentBlt",
                6:"sc_bootstrap_inspect",7:"sc_bootstrap_shutdown",8:"sc_bootstrap_system_path"}
    found = {int(match[1]):match[2] for match in re.finditer(r"(?m)^\s+(\d+)\s+[0-9A-F]+\s+[0-9A-F]+\s+(\w+)", exports)}
    if found != expected or "(forwarded to" in exports.lower():
        raise ValueError("Unexpected bootstrap exports or PE forwarders")
    for name in ("sentinel_core.dll","msimg32.dll","sentinel_probe.exe"):
        dependencies = run("dumpbin","/nologo","/dependents",str(bins / name)).lower()
        if any(word in dependencies for word in ("xinput","meathook","snaphak","vcruntime","msvcp")):
            raise ValueError(f"Unqualified runtime dependency: {name}")
    build(bins, args.output, commit, False)
    verify(args.output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--commit",required=True)
    parser.add_argument("--version",default="")
    parser.add_argument("--tag",default="")
    qualify(parser.parse_args())
