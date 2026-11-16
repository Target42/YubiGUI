#!/bin/sh
# Nimmt die Desktop-Verknüpfung wieder weg. Fremde Dateien mit demselben Namen bleiben.

desktop_dir() {
    home=$1
    dirs=$home/.config/user-dirs.dirs
    if [ -f "$dirs" ]; then
        line=$(grep -m 1 '^XDG_DESKTOP_DIR=' "$dirs" 2>/dev/null || true)
        value=${line#XDG_DESKTOP_DIR=}
        value=$(printf '%s' "$value" | tr -d '"')
        prefix='$HOME'
        case $value in
            "$prefix"|"$prefix"/*)
                value=$home${value#"$prefix"}
                ;;
        esac
        case $value in
            "$home"/*)
                printf '%s\n' "$value"
                return 0
                ;;
        esac
    fi
    if [ -d "$home/Desktop" ]; then
        printf '%s\n' "$home/Desktop"
        return 0
    fi
    if [ -d "$home/Schreibtisch" ]; then
        printf '%s\n' "$home/Schreibtisch"
        return 0
    fi
    return 0
}

remove_at() {
    target=$1/yubigui.desktop
    [ -f "$target" ] || return 0
    if grep -q '^Exec=yubigui$' "$target" 2>/dev/null; then
        rm -f "$target"
    fi
}

remove_home() {
    home=$1
    [ -d "$home" ] || return 0
    dest=$(desktop_dir "$home")
    [ -n "$dest" ] && remove_at "$dest"
    remove_at "$home/Desktop"
    remove_at "$home/Schreibtisch"
}

for home in /home/* /root; do
    remove_home "$home"
done

if [ -d /etc/skel ]; then
    remove_at /etc/skel/Desktop
    remove_at /etc/skel/Schreibtisch
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi

exit 0
