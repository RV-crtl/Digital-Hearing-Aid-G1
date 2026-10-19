# Security and safety disclosure

This is an educational wearable prototype, **not** an approved medical hearing aid, calibrated protective audio device or secure production Bluetooth appliance. Do not use it for clinical purposes.

Please report potential vulnerabilities or unsafe behavior privately to the repository maintainers through GitHub private communication rather than posting Bluetooth credentials, reproducible device compromises, or unsafe output procedures in a public issue. Include tested firmware commit, target board, observed behavior and minimal reproduction.

Priority issues include: uncontrolled sound output, unsafe power/battery behavior, crashes that fail to mute, buffer out-of-bounds, unexpected over-the-air commands and firmware integrity failure.

No guarantee of prompt security support, regulated conformity, or validated OTA update mechanism is currently provided.

## Source publication and vulnerability intake

Source hosting may be made public only after permissions, licences, confidential assets, full-history scanning, and the [publication gate](Documentation/PUBLICATION_GATE.md) are reviewed. A high-confidence current-tree credential scan is run in CI, but it is not a penetration test or proof that history contains no secrets.

For security-sensitive reports, prefer GitHub's **private vulnerability reporting** if the repository owner enables it, or another private channel agreed with maintainers. Do not upload keys, identifiable recordings, pairing data, or attack traces to public issues. Maintainers should acknowledge and coordinate fixes and advisories before making reproductions public. No authenticated OTA update system, device secure-boot chain or disclosure response-time SLA is currently established.

**Hardware matters:** do not connect this prototype to a human ear as a clinically safe amplifier; calibration, acoustic output caps, power protection, battery handling and fault-to-mute behavior require independent physical measurements.
