"""Utility to (re)generate the sample frame interpolation ONNX model shipped with Trident.

The renderer expects an ONNX model at Assets/AI/frame_generator.onnx. This helper emits a
minimal identity network that allows the runtime plumbing to initialise on machines that do
not yet have a production-quality frame generator available.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import onnx
from onnx import TensorProto, helper


def build_identity_model() -> onnx.ModelProto:
    """Create a trivial identity network that exercises the renderer's AI path."""

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
    onnx.save(l_Model, l_OutputPath)
    print(f"Wrote sample model to {l_OutputPath}")


if __name__ == "__main__":
    main()