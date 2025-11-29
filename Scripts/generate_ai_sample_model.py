def build_identity_model() -> Optional["onnx.ModelProto"]:
    if onnx is None:
        return None

    l_Input = helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, 2, 2])
    l_Output = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, 3, 2, 2])
    l_Node = helper.make_node("Identity", inputs=["input"], outputs=["output"], name="IdentityNode")

    l_Graph = helper.make_graph(nodes=[l_Node], name="FrameGeneratorIdentity", inputs=[l_Input], outputs=[l_Output])
    l_Model = helper.make_model(l_Graph, producer_name="TridentSampleFrameGenerator", opset_imports=[helper.make_opsetid("", 13)])

    l_Model.ir_version = 11
    for it_OpSet in l_Model.opset_import:
        it_OpSet.version = 13

    onnx.checker.check_model(l_Model)
    return l_Model