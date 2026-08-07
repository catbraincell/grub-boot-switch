if [ "$1" = 0 ]; then
    # The generator must be gone before grub.cfg is regenerated below.
    rm -f /etc/grub.d/99_grub-boot-switch
    if command -v update-grub >/dev/null 2>&1; then
        update-grub || true
    elif command -v grub2-mkconfig >/dev/null 2>&1; then
        grub2-mkconfig -o /boot/grub2/grub.cfg || true
    elif command -v grub-mkconfig >/dev/null 2>&1; then
        grub-mkconfig -o /boot/grub/grub.cfg || true
    fi
    if command -v udevadm >/dev/null 2>&1; then
        udevadm control --reload-rules || true
    fi
fi
exit 0
