import os, sys, shutil, subprocess, tarfile, zipfile

repo_dir = os.path.dirname(os.path.abspath(__file__))
os.chdir(repo_dir)

try:
    version_code = subprocess.check_output(["git", "rev-list", "--count", "HEAD"], text=True).strip()
    commit_hash = subprocess.check_output(["git", "log", "-1", "--pretty=%h"], text=True).strip()
    status = subprocess.check_output(["git", "status", "--porcelain"], text=True).strip()
    changes = "+" if status else "-"
except Exception:
    version_code = "87"
    commit_hash = "custom"
    changes = "+"

repackaged_str = f"repackagedhoch{version_code}"
version = f"v{version_code}{changes}({commit_hash})"
filename_ver = f"{repackaged_str}{changes}{commit_hash}"
zip_name = f"ViPER4AndroidFX-{filename_ver}.zip"

print(f"Building: {zip_name}")
print(f"Version: {version}, Code: {version_code}")

magisk_dir = os.path.join(repo_dir, "magiskmodule")

# Filter stockeqpackages.csv
if os.path.exists("stockeqpackages.csv"):
    with open("stockeqpackages.csv", "r", encoding="utf-8") as f_in:
        lines = [line.strip() for line in f_in if line.strip() and not line.strip().startswith("#")]
    with open(os.path.join(magisk_dir, "stockeqpackages.csv"), "w", encoding="utf-8", newline="\n") as f_out:
        f_out.write("\n".join(lines) + "\n")

# Copy module.prop
shutil.copy2("module.prop", os.path.join(magisk_dir, "module.prop"))

# Copy README.md
shutil.copy2("README.md", os.path.join(magisk_dir, "README.md"))

# Compress ViperIRS.tar.gz
irs_tar_path = os.path.join(magisk_dir, "ViperIRS.tar.gz")
print("Archiving ViperIRS...")
with tarfile.open(irs_tar_path, "w:gz", compresslevel=9) as tar:
    irs_dir = "ViperIRS"
    for fname in sorted(os.listdir(irs_dir)):
        if fname.endswith(".irs"):
            full_path = os.path.join(irs_dir, fname)
            tar.add(full_path, arcname=fname)
print(f"ViperIRS.tar.gz: {os.path.getsize(irs_tar_path):,} bytes")

# Compress ViperVDC.tar.gz
vdc_tar_path = os.path.join(magisk_dir, "ViperVDC.tar.gz")
print("Archiving ViperVDC...")
with tarfile.open(vdc_tar_path, "w:gz", compresslevel=9) as tar:
    vdc_dir = "ViperVDC"
    for fname in sorted(os.listdir(vdc_dir)):
        if fname.endswith(".vdc"):
            full_path = os.path.join(vdc_dir, fname)
            tar.add(full_path, arcname=fname)
print(f"ViperVDC.tar.gz: {os.path.getsize(vdc_tar_path):,} bytes")

# Create final flashable ZIP
zip_out_path = os.path.join(repo_dir, zip_name)
if os.path.exists(zip_out_path):
    os.remove(zip_out_path)

print("Packaging flashable ZIP module...")
with zipfile.ZipFile(zip_out_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
    for root, dirs, files in os.walk(magisk_dir):
        for f in files:
            full_p = os.path.join(root, f)
            arcname = os.path.relpath(full_p, magisk_dir).replace("\\", "/")
            zf.write(full_p, arcname)

universal_zip = os.path.join(repo_dir, "ViPER4AndroidFX-v2.7.2.1-KSU-Universal.zip")
shutil.copy2(zip_out_path, universal_zip)

print(f"\nSUCCESS! Built: {zip_out_path}")
print(f"Universal Copy: {universal_zip}")
print(f"File size: {os.path.getsize(zip_out_path)/(1024*1024):.2f} MB")
