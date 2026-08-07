# Defensive only: %attr(755,root,root) in CPACK_RPM_CONFIG_USER_FILELIST already
# pins it. A missing exec bit on the generator is a silent boot-config failure,
# so re-assert it anyway.
[ -e /etc/grub.d/99_grub-boot-switch ] && \
    chmod 0755 /etc/grub.d/99_grub-boot-switch
if command -v update-grub >/dev/null 2>&1; then
    update-grub || true
elif command -v grub2-mkconfig >/dev/null 2>&1; then
    grub2-mkconfig -o /boot/grub2/grub.cfg || true
elif command -v grub-mkconfig >/dev/null 2>&1; then
    grub-mkconfig -o /boot/grub/grub.cfg || true
fi
if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules || true
    udevadm trigger --subsystem-match=block || true
fi
exit 0
