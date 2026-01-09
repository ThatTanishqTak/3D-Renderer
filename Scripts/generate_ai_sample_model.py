from __future__ import annotations

import argparse
from pathlib import Path
from typing import Optional

try:
    import onnx
    from onnx import helper, TensorProto
except ImportError:  # pragma: no cover - handles environments without onnx installed.
    onnx = None
    helper = None
    TensorProto = None


def build_identity_model() -> Optional["onnx.ModelProto"]:
    # Return early when ONNX is not available so the caller can provide a helpful message.
    if onnx is None:
        return None

    l_Input = helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, 2, 2])
    l_Output = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, 3, 2, 2])
    l_Node = helper.make_node("Identity", inputs=["input"], outputs=["output"], name="IdentityNode")

    # Build a small graph so the engine has a deterministic sample model to load.
    l_Graph = helper.make_graph(nodes=[l_Node], name="FrameGeneratorIdentity", inputs=[l_Input], outputs=[l_Output])
    l_Model = helper.make_model(
        l_Graph,
        producer_name="TridentSampleFrameGenerator",
        opset_imports=[helper.make_opsetid("", 13)],
    )

    # Keep the IR and opset versions compatible with the bundled runtime.
    l_Model.ir_version = 11
    for it_OpSet in l_Model.opset_import:
        it_OpSet.version = 13

    onnx.checker.check_model(l_Model)
    return l_Model


def write_model(a_OutputPath: Path) -> int:
    # Create a sample model and write it to disk.
    l_Model = build_identity_model()
    if l_Model is None:
        print("ONNX is not installed. Install the onnx package to generate the model.")
        return 1

    a_OutputPath.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(l_Model, a_OutputPath.as_posix())
    print(f"Model written to {a_OutputPath.as_posix()}")
    return 0


def main() -> int:
    # Parse the output location from the command line.
    l_Parser = argparse.ArgumentParser(description="Generate the sample ONNX frame generator model.")
    l_Parser.add_argument("--output", required=True, help="Path to the output .onnx file.")
    l_Args = l_Parser.parse_args()

    l_OutputPath = Path(l_Args.output).resolve()
    return write_model(l_OutputPath)


if __name__ == "__main__":
    raise SystemExit(main())