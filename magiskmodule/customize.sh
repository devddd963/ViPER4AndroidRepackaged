#!/bin/sh

[ ! -f "$MODPATH"/constants.sh ] && abort "Missing constants.sh"
source "$MODPATH"/constants.sh

[ "$API" -lt "$MINAPI" ] && abort "Android SDK $API is not supported!"
[ "$BOOTMODE" != true ] && [ "$KSU" != true ] && abort "Please install this module within the Magisk or KernelSU app."

IFS=$'\n'
SEARCH_ROOT=/

rmi() {
  local file="$1"
  [ -f "$file" ] && rm "$file"
}

denylist_run() {
  [ -z "$KSU" ] && [ "$MAGISK_VER_CODE" -ge 27000 ] && magisk --denylist exec "$@" && return
  "$@"
}

ui_print "- Installing ViPER4Android driver binaries"
TARGET_ARCH="$ARCH"
[ -z "$TARGET_ARCH" ] && TARGET_ARCH="$(getprop ro.product.cpu.abi)"
case "$TARGET_ARCH" in
  *x86*|*x64*|*amd64*)
    ui_print "    Target architecture: x86 / x86_64 (Emulator)"
    DRIVER_SRC="$MODPATH/drivers/x86"
    ;;
  *)
    ui_print "    Target architecture: ARM / ARM64 (Device)"
    DRIVER_SRC="$MODPATH/drivers/arm"
    ;;
esac

mkdir -p "$MODPATH/system/vendor/lib/soundfx"
mkdir -p "$MODPATH/vendor/lib/soundfx"

if [ -f "$DRIVER_SRC/libv4a_fx.so" ]; then
  cp -f "$DRIVER_SRC/libv4a_fx.so" "$MODPATH/system/vendor/lib/soundfx/libv4a_fx.so"
  cp -f "$DRIVER_SRC/libv4a_fx.so" "$MODPATH/vendor/lib/soundfx/libv4a_fx.so"
fi

if [ -f "$DRIVER_SRC/libstdc++.so" ]; then
  cp -f "$DRIVER_SRC/libstdc++.so" "$MODPATH/system/vendor/lib/libstdc++.so"
  cp -f "$DRIVER_SRC/libstdc++.so" "$MODPATH/vendor/lib/libstdc++.so"
elif [ -f "/system/lib/libstdc++.so" ] && [ ! -f "/vendor/lib/libstdc++.so" ]; then
  cp -f "/system/lib/libstdc++.so" "$MODPATH/system/vendor/lib/libstdc++.so" 2>/dev/null
  cp -f "/system/lib/libstdc++.so" "$MODPATH/vendor/lib/libstdc++.so" 2>/dev/null
fi
rm -rf "$MODPATH/drivers"

# Create the scoped storage directory
mkdir -p "$FOLDER"

mkdir -p "$FOLDER"/DDC
CUSTOM_VDC_FILES=$(find "$SDCARD" -name '*.vdc' -not -path "$SDCARD/Android/*")
[ -n "$CUSTOM_VDC_FILES" ] && CUSTOM_VDC_FOUND=true || CUSTOM_VDC_FOUND=false
[ -f "$MODPATH"/"$VIPERVDCFILE" ] && VDC_ARCHIVE_FOUND=true || VDC_ARCHIVE_FOUND=false
for file in $(tar -tzf "$MODPATH"/"$VIPERVDCFILE") $CUSTOM_VDC_FILES; do
  file="$FOLDER"/DDC/"$file"
  rmi "$file"
done
[ -z "$(ls -A "$FOLDER"/DDC 2>/dev/null)" ] && VDC_FOLDER_EMPTY=true || VDC_FOLDER_EMPTY=false
if $VDC_FOLDER_EMPTY && ! $CUSTOM_VDC_FOUND && $VDC_ARCHIVE_FOUND; then
  ui_print "- Copying ViPER4Android VDCs"
  ui_print "   Check out https://t.me/vdcservice for custom ones"
  mkdir -p "$FOLDER"/DDC 2>/dev/null
  tar -xzf "$MODPATH"/"$VIPERVDCFILE" -C "$FOLDER"/DDC
else
  ui_print "- Skipping ViPER4Android VDC copy"
  ! $VDC_FOLDER_EMPTY && ui_print "    the folder is not empty"
  $CUSTOM_VDC_FOUND && ui_print "    custom VDCs have been found"
  ! $VDC_ARCHIVE_FOUND && ui_print "    the required data couldn't be located"
fi
rmi "$MODPATH"/"$VIPERVDCFILE"
if $CUSTOM_VDC_FOUND; then
  ui_print "- Copying custom VDCs"
  for file in $CUSTOM_VDC_FILES; do
    ui_print "    $file"
    cp -f "$file" "$FOLDER"/DDC
  done
fi

mkdir -p "$FOLDER"/Kernel
CUSTOM_IRS_FILES=$(find "$SDCARD" -name '*.irs' -not -path "$SDCARD/Android/*")
[ -n "$CUSTOM_IRS_FILES" ] && CUSTOM_IRS_FOUND=true || CUSTOM_IRS_FOUND=false
[ -f "$MODPATH"/"$VIPERIRSFILE" ] && IRS_ARCHIVE_FOUND=true || IRS_ARCHIVE_FOUND=false
for file in $(tar -tzf "$MODPATH"/"$VIPERIRSFILE") $CUSTOM_IRS_FILES; do
  file="$FOLDER"/Kernel/"$file"
  rmi "$file"
done
[ -z "$(ls -A "$FOLDER"/Kernel 2>/dev/null)" ] && IRS_FOLDER_EMPTY=true || IRS_FOLDER_EMPTY=false
if $IRS_FOLDER_EMPTY && ! $CUSTOM_IRS_FOUND && $IRS_ARCHIVE_FOUND; then
  ui_print "- Copying ViPER4Android IRS files"
  mkdir -p "$FOLDER"/Kernel 2>/dev/null
  tar -xzf "$MODPATH"/"$VIPERIRSFILE" -C "$FOLDER"/Kernel
else
  ui_print "- Skipping ViPER4Android IRS copy"
  ! $IRS_FOLDER_EMPTY && ui_print "    the folder is not empty"
  $CUSTOM_IRS_FOUND && ui_print "    custom IRS files have been found"
  ! $IRS_ARCHIVE_FOUND && ui_print "    the required data couldn't be located"
fi
rmi "$MODPATH"/"$VIPERIRSFILE"
if $CUSTOM_IRS_FOUND; then
  ui_print "- Copying custom IRS files"
  for file in $CUSTOM_IRS_FILES; do
    ui_print "    $file"
    cp -f "$file" "$FOLDER"/Kernel
  done
fi

presetCompatible() {
  local file="$1"
  grep -q '<int name="32775" value="[12]" />' < "$file" || return 1
  grep -q '<string name="65540;65541;65542">.*</string>' < "$file" || return 1
  return 0
}

mkdir -p "$FOLDER"/Preset
CUSTOM_PRESET_FILES=$(find "$SDCARD" -name '*.xml' -not -path "$SDCARD/Android/*")
if [ -n "$CUSTOM_PRESET_FILES" ]; then
  ui_print "- Copying custom preset files"
  for file in $CUSTOM_PRESET_FILES; do
    ! presetCompatible "$file" && continue
    ui_print "    $file"
    cp -f "$file" "$FOLDER"/Preset
  done
else
  ui_print "- Skipping preset copy"
  ui_print "    custom preset files have not been found"
fi

ui_print "- Patching system audio files"
osp_detect() {
  local file="$1"
  local osp=music
  case $file in
    *.conf)
      spaces=$(sed -n "/^output_session_processing {/,/^}/ {/^ *$osp {/p}" "$fil"e | sed -r "s/( *).*/\1/")
      effects=$(sed -n "/^output_session_processing {/,/^}/ {/^$spaces\$osp {/,/^$spaces}/p}" "$file" | grep -E "^$spaces +[A-Za-z]+" | sed -r "s/( *.*) .*/\1/g")
      for effect in ${effects}; do
        spaces=$(sed -n "/^effects {/,/^}/ {/^ *$effect {/p}" "$file" | sed -r "s/( *).*/\1/")
        sed -i "/^effects {/,/^}/ {/^$spaces$effect {/,/^$spaces}/d}" "$file"
      done
      ;;
    *.xml)
      effects=$(sed -n "/^ *<postprocess>$/,/^ *<\/postprocess>$/ {/^ *<stream type=\"$osp\">$/,/^ *<\/stream>$/ {/<stream type=\"$osp\">/d; /<\/stream>/d; s/<apply effect=\"//g; s/\"\/>//g; s/ *//g; p}}" "$file")
      for effect in ${effects}; do
        sed -i "/^\( *\)<apply effect=\"$effect\"\/>/d" "$file"
      done
      ;;
    *)
      abort "failed to run osp_detect on $file"
      ;;
  esac
}
AUDIO_EFFECTS_FILES="$( \
  find -H \
  $SEARCH_ROOT/system $SEARCH_ROOT/vendor $SEARCH_ROOT/odm \
  -type f -name "*audio_effects*.conf" -o -name "*audio_effects*.xml" \
  | sed "s|^$SEARCH_ROOT||" )"
for ORIGINAL_FILE in $AUDIO_EFFECTS_FILES; do
  ui_print "    Patching $ORIGINAL_FILE"
  FILE="$MODPATH"/"$(echo "$ORIGINAL_FILE" | sed -e 's|^/system/|/|g' -e 's|^/|/system/|')"
  [ -f "$FILE" ] && continue
  mkdir -p "$(dirname "$FILE")"
  ORIGINAL_FILE="$SEARCH_ROOT"/"$ORIGINAL_FILE"
  case "$FILE" in
    *.conf)
      denylist_run cat "$ORIGINAL_FILE" | \
      sed \
        -e "s|^effects {|effects {\n  $LIBRARY_NAME {\n    library $EFFECT_NAME\n    uuid $EFFECT_UUID\n  }|" \
        -e "s|^libraries {|libraries {\n  $EFFECT_NAME {\n    path $LIBRARY_FILE_PATH\n  }|" \
        > "$FILE"
      ;;
    *.xml)
      denylist_run cat "$ORIGINAL_FILE" | \
      sed \
        -e "s|<libraries>|<libraries>\n        <library name=\"$EFFECT_NAME\" path=\"$LIBRARY_FILE\"/>|" \
        -e "s|<effects>|<effects>\n        <effect name=\"$LIBRARY_NAME\" library=\"$EFFECT_NAME\" uuid=\"$EFFECT_UUID\"/>|" \
        > "$FILE"
      ;;
    *)
      abort "unable to patch $ORIGINAL_FILE"
      ;;
  esac
  osp_detect "$FILE"
  # Sync between $MODPATH/system/vendor and $MODPATH/vendor for Magisk / KernelSU OverlayFS compatibility
  case "$FILE" in
    "$MODPATH"/system/vendor/*)
      ALT_FILE="$MODPATH/vendor/${FILE#$MODPATH/system/vendor/}"
      mkdir -p "$(dirname "$ALT_FILE")"
      cp -f "$FILE" "$ALT_FILE"
      ;;
    "$MODPATH"/vendor/*)
      ALT_FILE="$MODPATH/system/vendor/${FILE#$MODPATH/vendor/}"
      mkdir -p "$(dirname "$ALT_FILE")"
      cp -f "$FILE" "$ALT_FILE"
      ;;
  esac
done
[ -z "$AUDIO_EFFECTS_FILES" ] && abort "Cant find any system audio configs!"

ui_print "- Installing ViPER4AndroidFX user app..."
TEMP_APK="$TMPDIR/v4afx.apk"
if [ -f "$MODPATH/v4afx.apk" ]; then
  cp -f "$MODPATH/v4afx.apk" "$TEMP_APK"
  rm -f "$MODPATH/v4afx.apk"
else
  unzip -oj "$ZIPFILE" "v4afx.apk" -d "$TMPDIR" >&2
fi

if [ -f "$TEMP_APK" ]; then
  pm install -r -d "$TEMP_APK" >/dev/null 2>&1 || pm install -r "$TEMP_APK" >/dev/null 2>&1 || pm install "$TEMP_APK" >/dev/null 2>&1
  rm -f "$TEMP_APK"
  ui_print "    Application installed successfully (User App)."
else
  abort "! ERROR: v4afx.apk could not be found or extracted."
fi

ui_print "- Configuring ViPER4Android"
VIPERFXPREFS="$(pm dump "$VIPERFXPACKAGE" | grep dataDir | head -n 1 | cut -d'=' -f2)"
VIPERFXPREFSOWNER="$(stat -c '%U' "$VIPERFXPREFS")"
VIPERFXSHAREDPREFS="$VIPERFXPREFS"/shared_prefs
[ ! -d "$VIPERFXSHAREDPREFS" ] && mkdir "$VIPERFXSHAREDPREFS"
cp -f "$MODPATH"/viperfx_preferences.xml "$VIPERFXSHAREDPREFS"/"${VIPERFXPACKAGE}_preferences.xml"
chown -R "$VIPERFXPREFSOWNER":"$VIPERFXPREFSOWNER" "$VIPERFXPREFS"
set_perm_recursive "$FOLDER" "$VIPERFXPREFSOWNER" sdcard_rw 771 660 u:object_r:sdcardfs:s0
# this permanently hides the notifications without the possiblity of reenabling them
[ "$API" -ge 31 ] && pm set-distracting-restriction --flag hide-notifications "$VIPERFXPACKAGE"
[ "$API" -ge 33 ] && {
  # this will tell android that the user made the final decision to not receive notifications
  pm revoke "$VIPERFXPACKAGE" android.permission.POST_NOTIFICATIONS
  pm set-permission-flags "$VIPERFXPACKAGE" android.permission.POST_NOTIFICATIONS user-fixed
}
# this disables battery optimization
[ "$API" -ge 30 ] && dumpsys deviceidle whitelist +"$VIPERFXPACKAGE" >&2
# this disables automatic permissions revoke if unused
[ "$API" -ge 30 ] && appops set --uid "$VIPERFXPACKAGE" AUTO_REVOKE_PERMISSIONS_IF_UNUSED ignore

while IFS= read -r packagedata; do
  package="$(echo "$packagedata" | cut -d'|' -f1)"
  package_filename="$(echo "$packagedata" | cut -d'|' -f2)"
  package_friendlyname="$(echo "$packagedata" | cut -d'|' -f3)"
  pm list packages | grep -q "^package:$package$" && {
    ui_print "- Disabling $package_friendlyname (p)"
    package_apk="$(pm list packages -f "$package" | grep -E "package:.*=$package$" | sed "s/package:\(.*\)=$package/\1/")"
    package_apk_dir="$(dirname "$package_apk" | sed -e 's|^/||' -e 's|^system/||')"
    mkdir -p "$MODPATH"/system/"$package_apk_dir"
    touch "$MODPATH"/system/"$package_apk_dir"/"$(basename "$package_apk")"
    continue
  }
  package_apk_files="$(find -H \
      $SEARCH_ROOT/system $SEARCH_ROOT/system_ext $SEARCH_ROOT/vendor $SEARCH_ROOT/product \
      -type f -name "$package_filename" \
      | sed -e "s|^$SEARCH_ROOT||" -e 's|^/system/|/|' \
      | uniq )"
  [ -n "$package_apk_files" ] && {
    ui_print "- Disabling $package_friendlyname (f)"
    for package_apk in $package_apk_files; do
      package_apk_dir="$(dirname "$package_apk" | sed -e 's|^/||' -e 's|^system/||')"
      mkdir -p "$MODPATH"/system/"$package_apk_dir"
      touch "$MODPATH"/system/"$package_apk_dir"/"$(basename "$package_apk")"
    done
  }
done < "$MODPATH"/stockeqpackages.csv

ui_print "- Setting Permissions"
set_perm_recursive "$MODPATH"/system root root 755 644
set_perm_recursive "$MODPATH"/system/vendor root root 755 644 u:object_r:vendor_file:s0
[ -d "$MODPATH"/system/vendor/etc ] && set_perm_recursive "$MODPATH"/system/vendor/etc root root 755 644 u:object_r:vendor_configs_file:s0
if [ -d "$MODPATH"/vendor ]; then
  set_perm_recursive "$MODPATH"/vendor root root 755 644 u:object_r:vendor_file:s0
  [ -d "$MODPATH"/vendor/etc ] && set_perm_recursive "$MODPATH"/vendor/etc root root 755 644 u:object_r:vendor_configs_file:s0
fi
