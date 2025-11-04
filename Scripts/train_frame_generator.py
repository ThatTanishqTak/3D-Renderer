"""Training entry point for the Trident frame interpolation model.

This script consumes a dataset of consecutive video frames and trains a lightweight
PyTorch model that predicts the intermediate frame. The converged model is exported to
ONNX so the renderer can load it via ``Renderer::ResolveAiModelPath`` or the
``TRIDENT_AI_MODEL`` override.
"""

from __future__ import annotations

import argparse
import json
import random
from dataclasses import dataclass
from pathlib import Path
from typing import List, Tuple

import torch
from torch import nn
from torch.utils.data import DataLoader, Dataset
from torchvision import transforms
from PIL import Image


@dataclass
class TrainingConfig:
    """Container describing the training hyper-parameters."""

    dataset_root: Path
    batch_size: int
    epochs: int
    learning_rate: float
    checkpoint_interval: int
    device: str
    export_path: Path
    checkpoint_path: Path
    image_size: int
    num_workers: int
    input_channels: int = 0


class ConsecutiveFrameDataset(Dataset):
    """Dataset that yields pairs of consecutive frames and the ground truth middle frame."""

    def __init__(self, dataset_root: Path, image_size: int) -> None:
        super().__init__()
        self.m_DatasetRoot = dataset_root
        self.m_ImageSize = image_size
        self.m_Triplets: List[Tuple[Path, Path, Path]] = []
        self.m_Transform = transforms.Compose(
            [
                transforms.Resize((self.m_ImageSize, self.m_ImageSize)),
                transforms.ToTensor(),
            ]
        )
        self._discover_triplets()

    def _discover_triplets(self) -> None:
        """Scan the dataset root for frame triplets.

        The expected layout is ``sequence_x/frame_000.png`` style directories where the file
        names are lexicographically sorted. Any directory with at least three frames
        contributes sliding triplets ``(frame_i, frame_{i+1}, frame_{i+2})``.
        """

        l_Sequences: List[Path] = [it_Path for it_Path in self.m_DatasetRoot.glob("**/*") if it_Path.is_dir()]
        if not l_Sequences:
            raise FileNotFoundError(
                f"No frame sequences discovered under {self.m_DatasetRoot}. Ensure you have extracted video frames."
            )

        for it_Sequence in sorted(l_Sequences):
            l_Frames: List[Path] = sorted(it_Sequence.glob("*.png")) + sorted(it_Sequence.glob("*.jpg"))
            if len(l_Frames) < 3:
                continue
            for l_Index in range(len(l_Frames) - 2):
                l_First = l_Frames[l_Index]
                l_Second = l_Frames[l_Index + 1]
                l_Third = l_Frames[l_Index + 2]
                self.m_Triplets.append((l_First, l_Second, l_Third))

        if not self.m_Triplets:
            raise FileNotFoundError(
                f"Dataset root {self.m_DatasetRoot} did not contain any usable frame triplets."
            )

    def __len__(self) -> int:
        return len(self.m_Triplets)

    def __getitem__(self, index: int) -> Tuple[torch.Tensor, torch.Tensor]:
        l_FirstPath, l_MiddlePath, l_ThirdPath = self.m_Triplets[index]
        l_FirstFrame = self._load_frame(l_FirstPath)
        l_ThirdFrame = self._load_frame(l_ThirdPath)
        l_TargetFrame = self._load_frame(l_MiddlePath)
        l_Input = torch.cat([l_FirstFrame, l_ThirdFrame], dim=0)
        return l_Input, l_TargetFrame

    def _load_frame(self, frame_path: Path) -> torch.Tensor:
        l_Image = Image.open(frame_path).convert("RGB")
        return self.m_Transform(l_Image)


class SimpleInterpolationNet(nn.Module):
    """Small encoder-decoder network for frame interpolation."""

    def __init__(self) -> None:
        super().__init__()
        self.m_Encoder = nn.Sequential(
            nn.Conv2d(6, 32, kernel_size=3, padding=1),
            nn.ReLU(inplace=True),
            nn.Conv2d(32, 64, kernel_size=3, stride=2, padding=1),
            nn.ReLU(inplace=True),
            nn.Conv2d(64, 128, kernel_size=3, stride=2, padding=1),
            nn.ReLU(inplace=True),
        )
        self.m_Decoder = nn.Sequential(
            nn.ConvTranspose2d(128, 64, kernel_size=4, stride=2, padding=1),
            nn.ReLU(inplace=True),
            nn.ConvTranspose2d(64, 32, kernel_size=4, stride=2, padding=1),
            nn.ReLU(inplace=True),
            nn.Conv2d(32, 3, kernel_size=3, padding=1),
            nn.Sigmoid(),
        )

    def forward(self, l_Input: torch.Tensor) -> torch.Tensor:
        l_Feature = self.m_Encoder(l_Input)
        l_Output = self.m_Decoder(l_Feature)
        return l_Output


def parse_arguments() -> TrainingConfig:
    """Parse CLI arguments into a TrainingConfig."""

    l_Parser = argparse.ArgumentParser(description=__doc__)
    l_Parser.add_argument("dataset", type=Path, help="Root directory containing consecutive frame folders.")
    l_Parser.add_argument("--batch-size", type=int, default=8, dest="batch_size")
    l_Parser.add_argument("--epochs", type=int, default=20)
    l_Parser.add_argument("--learning-rate", type=float, default=1e-4, dest="learning_rate")
    l_Parser.add_argument("--checkpoint-interval", type=int, default=5, dest="checkpoint_interval")
    l_Parser.add_argument(
        "--export-path",
        type=Path,
        default=Path("Assets/AI/frame_generator.onnx"),
        dest="export_path",
        help="Destination for the exported ONNX model.",
    )
    l_Parser.add_argument(
        "--checkpoint-path",
        type=Path,
        default=Path("Assets/AI/checkpoints/frame_generator.pt"),
        dest="checkpoint_path",
        help="Directory where intermediate checkpoints are stored.",
    )
    l_Parser.add_argument("--image-size", type=int, default=256, dest="image_size")
    l_Parser.add_argument("--device", type=str, default="cuda" if torch.cuda.is_available() else "cpu", dest="device")
    l_Parser.add_argument("--num-workers", type=int, default=4, dest="num_workers")
    l_Parser.add_argument(
        "--seed", type=int, default=1234, help="Random seed for reproducibility.", dest="seed"
    )
    l_Args = l_Parser.parse_args()

    random.seed(l_Args.seed)
    torch.manual_seed(l_Args.seed)

    return TrainingConfig(
        dataset_root=l_Args.dataset,
        batch_size=l_Args.batch_size,
        epochs=l_Args.epochs,
        learning_rate=l_Args.learning_rate,
        checkpoint_interval=l_Args.checkpoint_interval,
        device=l_Args.device,
        export_path=l_Args.export_path,
        checkpoint_path=l_Args.checkpoint_path,
        image_size=l_Args.image_size,
        num_workers=l_Args.num_workers,
    )


def create_dataloader(a_Config: TrainingConfig) -> DataLoader:
    l_Dataset = ConsecutiveFrameDataset(a_Config.dataset_root, a_Config.image_size)
    if len(l_Dataset) == 0:
        raise RuntimeError("Dataset did not yield any samples.")

    # Peek at the first sample to capture the concatenated channel count for ONNX export.
    l_SampleInput, _ = l_Dataset[0]
    a_Config.input_channels = l_SampleInput.shape[0]

    l_Loader = DataLoader(
        l_Dataset,
        batch_size=a_Config.batch_size,
        shuffle=True,
        num_workers=a_Config.num_workers,
        pin_memory=a_Config.device == "cuda",
    )
    return l_Loader


def train_model(a_Config: TrainingConfig) -> SimpleInterpolationNet:
    """Train the interpolation network and periodically persist checkpoints."""

    l_Device = torch.device(a_Config.device)
    l_Model = SimpleInterpolationNet().to(l_Device)
    l_Criterion = nn.L1Loss()
    l_Optimizer = torch.optim.Adam(l_Model.parameters(), lr=a_Config.learning_rate)

    l_Loader = create_dataloader(a_Config)
    a_Config.checkpoint_path.parent.mkdir(parents=True, exist_ok=True)
    a_Config.export_path.parent.mkdir(parents=True, exist_ok=True)

    for l_Epoch in range(1, a_Config.epochs + 1):
        l_Model.train()
        l_RunningLoss = 0.0
        for l_BatchIndex, (l_Input, l_Target) in enumerate(l_Loader):
            l_Input = l_Input.to(l_Device)
            l_Target = l_Target.to(l_Device)

            # Forward pass predicts the missing middle frame.
            l_Prediction = l_Model(l_Input)
            l_Loss = l_Criterion(l_Prediction, l_Target)

            l_Optimizer.zero_grad(set_to_none=True)
            l_Loss.backward()
            l_Optimizer.step()

            l_RunningLoss += l_Loss.item()

            if (l_BatchIndex + 1) % 10 == 0:
                print(
                    json.dumps(
                        {
                            "epoch": l_Epoch,
                            "batch": l_BatchIndex + 1,
                            "loss": l_Loss.item(),
                        }
                    )
                )

        l_EpochLoss = l_RunningLoss / max(len(l_Loader), 1)
        print(json.dumps({"epoch": l_Epoch, "average_loss": l_EpochLoss}))

        if l_Epoch % a_Config.checkpoint_interval == 0:
            l_CheckpointFile = a_Config.checkpoint_path.with_name(
                f"frame_generator_epoch_{l_Epoch:04d}.pt"
            )
            # Persist checkpoints so long training runs can resume after interruptions.
            torch.save({"model_state": l_Model.state_dict(), "epoch": l_Epoch}, l_CheckpointFile)
            print(f"Saved checkpoint to {l_CheckpointFile}")

    return l_Model


def export_model(a_Model: SimpleInterpolationNet, a_Config: TrainingConfig) -> None:
    """Export the trained model to ONNX so the renderer can load it."""

    a_Model.eval()
    l_Device = torch.device(a_Config.device)
    if a_Config.input_channels <= 0:
        raise RuntimeError("Input channel count must be discovered during training before exporting ONNX.")

    # Dummy tensor encodes the renderer readback layout (batch, channels, height, width).
    l_DummyInput = torch.zeros((1, a_Config.input_channels, a_Config.image_size, a_Config.image_size), device=l_Device)
    l_OutputPath = a_Config.export_path
    torch.onnx.export(
        a_Model,
        l_DummyInput,
        l_OutputPath,
        input_names=["input"],
        output_names=["output"],
        opset_version=17,
        dynamic_axes={
            "input": {0: "batch", 2: "height", 3: "width"},
            "output": {0: "batch", 2: "height", 3: "width"},
        },
    )
    print(f"Exported ONNX model to {l_OutputPath}")


def main() -> None:
    l_Config = parse_arguments()
    print(json.dumps({"config": l_Config.__dict__}, default=str))
    l_Model = train_model(l_Config)
    export_model(l_Model, l_Config)


if __name__ == "__main__":
    main()