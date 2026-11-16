#!/bin/sh
# Menüeintrag und Icon legt das Paket nach /usr. Zusätzlich bekommt jeder
# vorhandene Desktop eine Verknüpfung, sofern der Desktop-Ordner bekannt ist.

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

mark_trusted() {
    owner=$1
    target=$2
    command -v gio >/dev/null 2>&1 || return 0
    command -v runuser >/dev/null 2>&1 || return 0
    uid=$(id -u "$owner" 2>/dev/null || true)
    [ -n "$uid" ] || return 0
    bus=/run/user/$uid/bus
    [ -S "$bus" ] || return 0
    runuser -u "$owner" -- env \
        XDG_RUNTIME_DIR=/run/user/$uid \
        DBUS_SESSION_BUS_ADDRESS=unix:path=$bus \
        gio set "$target" metadata::trusted true >/dev/null 2>&1 || true
}

place_link() {
    home=$1
    owner=$2
    [ -d "$home" ] || return 0
    dest=$(desktop_dir "$home")
    [ -n "$dest" ] || return 0
    created=0
    if [ ! -d "$dest" ]; then
        mkdir -p "$dest" 2>/dev/null || return 0
        created=1
    fi
    target=$dest/yubigui.desktop
    if [ -e "$target" ] && ! grep -q '^Exec=yubigui$' "$target" 2>/dev/null; then
        return 0
    fi
    cp -f /usr/share/applications/yubigui.desktop "$target" || return 0
    chmod 755 "$target" || true
    if [ -n "$owner" ]; then
        chown "$owner:$owner" "$target" 2>/dev/null || chown "$owner" "$target" 2>/dev/null || true
        if [ "$created" -eq 1 ]; then
            chown "$owner:$owner" "$dest" 2>/dev/null || chown "$owner" "$dest" 2>/dev/null || true
        fi
    fi
    mark_trusted "$owner" "$target"
}

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi

if [ -d /etc/skel ]; then
    for name in Desktop Schreibtisch; do
        if [ -d "/etc/skel/$name" ]; then
            cp -f /usr/share/applications/yubigui.desktop "/etc/skel/$name/yubigui.desktop" || true
            chmod 755 "/etc/skel/$name/yubigui.desktop" || true
        fi
    done
fi

for home in /home/* /root; do
    [ -d "$home" ] || continue
    place_link "$home" "$(basename "$home")"
done

exit 0
