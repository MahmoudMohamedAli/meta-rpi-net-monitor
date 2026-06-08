SUMMARY = "RPi Network Monitor Python Bridge + Web Dashboard"
DESCRIPTION = "stdlib http bridge that receives UDP telemetry from the C++ daemon \
and serves a live WebSocket dashboard on port 8080."
HOMEPAGE = "https://github.com/mahmoud/rpi-net-monitor"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=760fde9774ff6ebbfcec22bf2f28377e"

SRC_URI = " \
    git://github.com/MahmoudMohamedAli/meta-rpi-net-monitor.git;protocol=https;branch=main \
    file://netmon-bridge.service \
"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

# Python runtime dependencies
RDEPENDS:${PN} = " \
    python3 \
    python3-asyncio \
    rpi-net-monitor-daemon \
"

inherit systemd 

SYSTEMD_SERVICE:${PN} = "netmon-bridge.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    # Python bridge
    install -d ${D}/opt/netmon
    install -m 0644 ${S}/bridge/bridge.py ${D}/opt/netmon/bridge.py
    install -m 0644 ${S}/bridge/requirements.txt ${D}/opt/netmon/requirements.txt

    # Web dashboard
    install -d ${D}/opt/netmon/webui
    install -m 0644 ${S}/webui/index.html ${D}/opt/netmon/webui/index.html

    # systemd unit
    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${WORKDIR}/netmon-bridge.service \
        ${D}${systemd_unitdir}/system/
}

FILES:${PN} += " \
    /opt/netmon \
    ${systemd_unitdir}/system/netmon-bridge.service \
"
