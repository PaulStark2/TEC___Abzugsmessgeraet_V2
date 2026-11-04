from pathlib import Path
import re

root = Path(__file__).resolve().parent
gen = (root / "_wire_gen.txt").read_text(encoding="utf-8")
parts = {}
cur = None
buf = []
for line in gen.splitlines():
    if line in ("STRUCT:", "ENC:", "DEC:", "JS:"):
        if cur:
            parts[cur] = "\n".join(buf).strip("\n")
        cur = line[:-1]
        buf = []
    elif line.startswith("WIRE="):
        parts["WIRE"] = line.split("=", 1)[1].strip()
    else:
        buf.append(line)
if cur:
    parts[cur] = "\n".join(buf).strip("\n")

wire = parts["WIRE"]

# ProtocolSpec
ps = root / "communication" / "ProtocolSpec.hpp"
text = ps.read_text(encoding="utf-8")
text = re.sub(
    r"// Prior .*?\nconstexpr std::size_t kMachineConfigPayloadWireSize = \d+;",
    f"// Bore-based probe calib wire (Messtaster fields removed).\nconstexpr std::size_t kMachineConfigPayloadWireSize = {wire};",
    text,
    count=1,
    flags=re.S,
)
ps.write_text(text, encoding="utf-8")

# protocolTypes.js
pt = root / "gui" / "server" / "protocol" / "protocolTypes.js"
text = pt.read_text(encoding="utf-8")
text = re.sub(
    r"const MACHINE_CONFIG_PAYLOAD_WIRE_SIZE = \d+;",
    f"const MACHINE_CONFIG_PAYLOAD_WIRE_SIZE = {wire};",
    text,
    count=1,
)
pt.write_text(text, encoding="utf-8")

# Payloads.hpp — replace MachineConfigPayload body
pl = root / "communication" / "Payloads.hpp"
text = pl.read_text(encoding="utf-8")
text = re.sub(
    r"struct MachineConfigPayload\n\{.*?\n\};",
    "struct MachineConfigPayload\n{\n" + parts["STRUCT"] + "\n};",
    text,
    count=1,
    flags=re.S,
)
pl.write_text(text, encoding="utf-8")

# PayloadCodec encode/decode bodies
pc = root / "communication" / "PayloadCodec.cpp"
text = pc.read_text(encoding="utf-8")
text = re.sub(
    r"(bool encodeMachineConfigPayload\(const MachineConfigPayload& payload, uint8_t\* out, std::size_t capacity\)\n\{\n    if \(payload\.preclamp_confirm > 1 \|\| payload\.anomaly_skip > 1\)\n    \{\n        return false;\n    \}\n    std::size_t offset = 0;\n    const bool ok =\n)(.*?)(\n    return ok && offset == kMachineConfigPayloadWireSize && payload\.anomaly_skip <= 1;\n\})",
    r"\1" + parts["ENC"] + r";\3",
    text,
    count=1,
    flags=re.S,
)
text = re.sub(
    r"(bool decodeMachineConfigPayload\(const uint8_t\* data, std::size_t length, MachineConfigPayload& out\)\n\{\n    std::size_t offset = 0;\n    const bool ok =\n)(.*?)(\n    return ok && offset == length && length == kMachineConfigPayloadWireSize &&\n           out\.preclamp_confirm <= 1 && out\.anomaly_skip <= 1;\n\})",
    r"\1" + parts["DEC"] + r";\3",
    text,
    count=1,
    flags=re.S,
)
pc.write_text(text, encoding="utf-8")

# m12Codec MACHINE_CONFIG_FIELDS
m12 = root / "gui" / "server" / "protocol" / "m12Codec.js"
text = m12.read_text(encoding="utf-8")
text = re.sub(
    r"const MACHINE_CONFIG_FIELDS = \[\n.*?\n\];",
    "const MACHINE_CONFIG_FIELDS = [\n" + parts["JS"] + "\n];",
    text,
    count=1,
    flags=re.S,
)
m12.write_text(text, encoding="utf-8")

print("applied wire", wire)
