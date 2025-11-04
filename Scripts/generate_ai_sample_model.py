"""Utility to (re)generate the sample frame interpolation ONNX model shipped with Trident.

The renderer expects an ONNX model at Assets/AI/frame_generator.onnx. This helper emits a
minimal identity network that allows the runtime plumbing to initialise on machines that do
not yet have a production-quality frame generator available. Prefer the trained artifact
emitted by ``Scripts/train_frame_generator.py`` when packaging builds.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import base64
from importlib import import_module, util
from typing import Optional

l_OnnxSpec = util.find_spec("onnx")
if l_OnnxSpec is not None:
    onnx = import_module("onnx")
    from onnx import TensorProto, helper
else:
    onnx = None  # type: ignore[assignment]
    TensorProto = helper = None  # type: ignore[assignment]


# Precomputed binary for the diagnostic identity network so we can continue without onnx.
FALLBACK_MODEL_BASE64 = (
    "CAwSG1RyaWRlbnRTYW1wbGVGcmFtZUdlbmVyYXRvcjqEAQonCgVpbnB1dBIGb3V0cHV0GgxJZGVu"
    "dGl0eU5vZGUiCElkZW50aXR5EhZGcmFtZUdlbmVyYXRvcklkZW50aXR5Wh8KBWlucHV0EhYKFAgB"
    "EhAKAggBCgIIAwoCCAIKAggCYiAKBm91dHB1dBIWChQIARIQCgIIAQoCCAMKAggCCgIIAkIECgAQ"
    "DQ=="
)


def build_identity_model() -> Optional["onnx.ModelProto"]:
    """Create a trivial identity network that exercises the renderer's AI path."""

    if onnx is None:
        return None

    l_Input = helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, 2, 2])
    l_Output = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, 3, 2, 2])
    l_Node = helper.make_node("Identity", inputs=["input"], outputs=["output"], name="IdentityNode")

    l_Graph = helper.make_graph(nodes=[l_Node], name="FrameGeneratorIdentity", inputs=[l_Input], outputs=[l_Output])
    l_Model = helper.make_model(
        l_Graph,
        producer_name="TridentSampleFrameGenerator",
        opset_imports=[helper.make_opsetid("", 13)],
    )
    onnx.checker.check_model(l_Model)
    return l_Model


def emit_embedded_model(a_OutputPath: Path) -> None:
    """Write the built-in diagnostic model when the onnx package is unavailable."""

    l_OutputPathBytes = base64.b64decode(FALLBACK_MODEL_BASE64)
    a_OutputPath.write_bytes(l_OutputPathBytes)


def main() -> None:
    l_Parser = argparse.ArgumentParser(description=__doc__)
    l_Parser.add_argument(
        "--output",
        type=Path,
        default=Path("Assets/AI/frame_generator.onnx"),
        help="Destination path for the generated ONNX model.",
    )
    l_Arguments = l_Parser.parse_args()

    l_OutputPath = l_Arguments.output
    l_OutputPath.parent.mkdir(parents=True, exist_ok=True)

    l_Model = build_identity_model()
    if l_Model is not None:
        # onnx is available locally, so we emit a freshly generated model.
        onnx.save(l_Model, l_OutputPath)
        print(f"Wrote sample model to {l_OutputPath}")
    else:
        # Fall back to the embedded diagnostic asset so the build can still proceed.
        emit_embedded_model(l_OutputPath)
        print(
            "onnx package missing. Wrote embedded diagnostic model instead; install onnx for regenerated assets."
        )


if __name__ == "__main__":
    main()