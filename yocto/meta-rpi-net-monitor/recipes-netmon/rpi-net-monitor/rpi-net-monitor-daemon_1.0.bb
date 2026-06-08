SUMMARY = "RPi Network Telemetry Daemon (C++)"
DESCRIPTION = "Polls /proc/net/dev, drives a link-state FSM, and publishes \
telemetry over UDP to the Python bridge."
HOMEPAGE = "https://github.com/mahmoud/rpi-net-monitor"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://../LICENSE;md5=760fde9774ff6ebbfcec22bf2f28377e"

# Adjust SRC_URI to point at your Git repo or local source
SRC_URI = " \
    git://github.com/MahmoudMohamedAli/meta-rpi-net-monitor.git;protocol=https;branch=main \
    file://netmon.service \
"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git/daemon"

# Build-time dependencies
DEPENDS = ""

inherit cmake systemd

# CMake options
EXTRA_OECMAKE = " \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF \
"

# systemd integration
SYSTEMD_SERVICE:${PN} = "netmon.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    # Install systemd unit
    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${WORKDIR}/netmon.service ${D}${systemd_unitdir}/system/
}

# Add netmon system user
inherit useradd
USERADD_PACKAGES = "${PN}"
USERADD_PARAM:${PN} = \
    "--system --no-create-home --shell /bin/false --user-group netmon"

FILES:${PN} += " \
    ${sbindir}/netmon \
    ${systemd_unitdir}/system/netmon.service \
"
