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
from typing import List, Optional, Tuple

import torch
from torch import nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, Dataset, random_split
from torchvision import transforms
from PIL import Image


@dataclass
class TrainingConfig:
    """Container describing the training hyper-parameters."""

    dataset_root: Optional[Path]
    batch_size: int
    epochs: int
    learning_rate: float
    checkpoint_interval: int
    device: str
    export_path: Path
    checkpoint_path: Path
    image_size: int
    num_workers: int
    seed: int
    validation_split: float
    early_stop_patience: int
    early_stop_min_delta: float
    input_channels: int = 0
    skip_training: bool = False
    opset_version: int = 17
    ir_version: int = 11


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


class ResidualBlock(nn.Module):
    """Residual building block used throughout the interpolation network."""

    def __init__(self, a_Channels: int) -> None:
        super().__init__()
        self.m_Block = nn.Sequential(
            nn.Conv2d(a_Channels, a_Channels, kernel_size=3, padding=1, bias=False),
            nn.BatchNorm2d(a_Channels),
            nn.ReLU(inplace=True),
            nn.Conv2d(a_Channels, a_Channels, kernel_size=3, padding=1, bias=False),
            nn.BatchNorm2d(a_Channels),
        )
        self.m_Activation = nn.ReLU(inplace=True)

    def forward(self, l_Input: torch.Tensor) -> torch.Tensor:
        l_Output = self.m_Block(l_Input) + l_Input
        return self.m_Activation(l_Output)


class InterpolationUNet(nn.Module):
    """Residual U-Net style architecture for high quality frame interpolation."""

    def __init__(self, a_InputChannels: int) -> None:
        super().__init__()

        # Encoder progressively downsamples the spatial resolution while capturing context.
        self.m_EncoderStage1 = nn.Sequential(
            nn.Conv2d(a_InputChannels, 32, kernel_size=3, padding=1),
            nn.ReLU(inplace=True),
            ResidualBlock(32),
        )
        self.m_EncoderStage2 = nn.Sequential(
            nn.Conv2d(32, 64, kernel_size=3, stride=2, padding=1),
            nn.ReLU(inplace=True),
            ResidualBlock(64),
        )
        self.m_EncoderStage3 = nn.Sequential(
            nn.Conv2d(64, 128, kernel_size=3, stride=2, padding=1),
            nn.ReLU(inplace=True),
            ResidualBlock(128),
        )

        # Bottleneck keeps a wide receptive field for temporal reasoning.
        self.m_Bottleneck = nn.Sequential(
            ResidualBlock(128),
            ResidualBlock(128),
        )

        # Decoder restores the original resolution and merges encoder skip connections.
        self.m_DecodeStage2 = nn.Sequential(
            nn.ConvTranspose2d(128, 64, kernel_size=4, stride=2, padding=1),
            nn.ReLU(inplace=True),
            ResidualBlock(64),
        )
        self.m_DecodeStage1 = nn.Sequential(
            nn.ConvTranspose2d(64, 32, kernel_size=4, stride=2, padding=1),
            nn.ReLU(inplace=True),
            ResidualBlock(32),
        )
        self.m_OutputLayer = nn.Sequential(
            nn.Conv2d(32, 3, kernel_size=3, padding=1),
            nn.Sigmoid(),
        )

    def forward(self, l_Input: torch.Tensor) -> torch.Tensor:
        # Downsample while retaining skip tensors for the U-Net structure.
        l_Skip1 = self.m_EncoderStage1(l_Input)
        l_Skip2 = self.m_EncoderStage2(l_Skip1)
        l_BottleneckInput = self.m_EncoderStage3(l_Skip2)

        # Bottleneck performs the heavy lifting for denoising/interpolation.
        l_BottleneckOutput = self.m_Bottleneck(l_BottleneckInput)

        # Decoder with skip connections for detail preservation.
        l_Decode2 = self.m_DecodeStage2(l_BottleneckOutput) + l_Skip2
        l_Decode1 = self.m_DecodeStage1(l_Decode2) + l_Skip1
        l_Output = self.m_OutputLayer(l_Decode1)
        return l_Output


def _build_gaussian_kernel(a_WindowSize: int, a_Sigma: float, a_Channels: int, a_Device: torch.device) -> torch.Tensor:
    """Create a Gaussian kernel expanded for depth-wise SSIM convolutions."""

    l_Axis = torch.arange(a_WindowSize, dtype=torch.float32, device=a_Device) - a_WindowSize // 2
    l_Kernel1D = torch.exp(-(l_Axis ** 2) / (2 * (a_Sigma ** 2)))
    l_Kernel1D = l_Kernel1D / torch.sum(l_Kernel1D)
    l_Kernel2D = torch.outer(l_Kernel1D, l_Kernel1D)
    l_Kernel2D = l_Kernel2D.expand(a_Channels, 1, a_WindowSize, a_WindowSize)
    return l_Kernel2D


def compute_psnr(a_Prediction: torch.Tensor, a_Target: torch.Tensor) -> float:
    """Compute the mean Peak Signal-to-Noise Ratio for a batch of frames."""

    l_Epsilon = 1e-8
    l_Mse = torch.mean((a_Prediction - a_Target) ** 2, dim=(1, 2, 3))
    l_Psnr = 10.0 * torch.log10((1.0 ** 2) / (l_Mse + l_Epsilon))
    return float(l_Psnr.mean().item())


def compute_ssim(a_Prediction: torch.Tensor, a_Target: torch.Tensor) -> float:
    """Compute the Structural Similarity Index (SSIM) for a batch of frames."""

    s_WindowSize = 11
    s_Sigma = 1.5
    s_C1 = 0.01 ** 2
    s_C2 = 0.03 ** 2

    l_Channels = a_Prediction.size(1)
    l_Device = a_Prediction.device
    l_Kernel = _build_gaussian_kernel(s_WindowSize, s_Sigma, l_Channels, l_Device)

    l_MuPrediction = F.conv2d(a_Prediction, l_Kernel, padding=s_WindowSize // 2, groups=l_Channels)
    l_MuTarget = F.conv2d(a_Target, l_Kernel, padding=s_WindowSize // 2, groups=l_Channels)

    l_MuPredictionSq = l_MuPrediction ** 2
    l_MuTargetSq = l_MuTarget ** 2
    l_MuPredictionTarget = l_MuPrediction * l_MuTarget

    l_SigmaPrediction = F.conv2d(a_Prediction * a_Prediction, l_Kernel, padding=s_WindowSize // 2, groups=l_Channels) - l_MuPredictionSq
    l_SigmaTarget = F.conv2d(a_Target * a_Target, l_Kernel, padding=s_WindowSize // 2, groups=l_Channels) - l_MuTargetSq
    l_SigmaPredictionTarget = (
        F.conv2d(a_Prediction * a_Target, l_Kernel, padding=s_WindowSize // 2, groups=l_Channels) - l_MuPredictionTarget
    )

    l_Numerator = (2 * l_MuPredictionTarget + s_C1) * (2 * l_SigmaPredictionTarget + s_C2)
    l_Denominator = (l_MuPredictionSq + l_MuTargetSq + s_C1) * (l_SigmaPrediction + l_SigmaTarget + s_C2)
    l_SsimMap = l_Numerator / (l_Denominator + 1e-8)
    l_Ssim = l_SsimMap.mean()
    return float(l_Ssim.item())


def parse_arguments() -> TrainingConfig:
    """Parse CLI arguments into a TrainingConfig."""

    l_Parser = argparse.ArgumentParser(description=__doc__)
    l_Parser.add_argument("dataset", type=Path, nargs="?", help="Root directory containing consecutive frame folders.")
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
    l_Parser.add_argument(
        "--validation-split",
        type=float,
        default=0.1,
        dest="validation_split",
        help="Fraction of the dataset to dedicate to validation metrics.",
    )
    l_Parser.add_argument(
        "--early-stop-patience",
        type=int,
        default=5,
        dest="early_stop_patience",
        help="Number of epochs to wait for validation improvement before stopping early.",
    )
    l_Parser.add_argument(
        "--early-stop-min-delta",
        type=float,
        default=0.05,
        dest="early_stop_min_delta",
        help="Minimum PSNR improvement required to reset the early stopping counter.",
    )
    l_Parser.add_argument(
        "--skip-training",
        action="store_true",
        help="Skip dataset loading and emit an untrained export (useful for CI asset refreshes).",
    )
    l_Parser.add_argument(
        "--input-channels",
        type=int,
        default=6,
        dest="input_channels",
        help="Input channel count to use when skipping training (defaults to two RGB frames).",
    )
    l_Parser.add_argument(
        "--opset-version",
        type=int,
        default=17,
        dest="opset_version",
        help="Target ONNX opset version compatible with the bundled runtime (IR 11).",
    )
    l_Parser.add_argument(
        "--ir-version",
        type=int,
        default=11,
        dest="ir_version",
        help="Explicit ONNX IR version to stamp on the exported model for MSVC/VS2022 builds.",
    )
    l_Args = l_Parser.parse_args()

    if not l_Args.skip_training and l_Args.dataset is None:
        raise SystemExit("Dataset path is required unless --skip-training is supplied.")

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
        seed=l_Args.seed,
        validation_split=l_Args.validation_split,
        early_stop_patience=l_Args.early_stop_patience,
        early_stop_min_delta=l_Args.early_stop_min_delta,
        skip_training=l_Args.skip_training,
        input_channels=l_Args.input_channels,
        opset_version=l_Args.opset_version,
        ir_version=l_Args.ir_version,
    )


def create_dataloaders(a_Config: TrainingConfig) -> Tuple[DataLoader, DataLoader]:
    """Create training and validation data loaders using a configurable split."""

    if a_Config.dataset_root is None:
        raise RuntimeError("Dataset root must be provided when training is enabled.")

    l_Dataset = ConsecutiveFrameDataset(a_Config.dataset_root, a_Config.image_size)
    if len(l_Dataset) == 0:
        raise RuntimeError("Dataset did not yield any samples.")

    # Peek at the first sample to capture the concatenated channel count for ONNX export.
    l_SampleInput, _ = l_Dataset[0]
    a_Config.input_channels = l_SampleInput.shape[0]

    l_TotalSamples = len(l_Dataset)
    l_ValidationSize = max(int(l_TotalSamples * a_Config.validation_split), 1)
    l_TrainingSize = max(l_TotalSamples - l_ValidationSize, 1)
    if l_TrainingSize + l_ValidationSize > l_TotalSamples:
        l_ValidationSize = l_TotalSamples - l_TrainingSize

    # Use a deterministic generator so runs are reproducible with the CLI seed.
    l_Generator = torch.Generator().manual_seed(a_Config.seed)
    l_TrainDataset, l_ValidationDataset = random_split(
        l_Dataset,
        [l_TrainingSize, l_ValidationSize],
        generator=l_Generator,
    )

    l_TrainLoader = DataLoader(
        l_TrainDataset,
        batch_size=a_Config.batch_size,
        shuffle=True,
        num_workers=a_Config.num_workers,
        pin_memory=a_Config.device == "cuda",
    )
    l_ValidationLoader = DataLoader(
        l_ValidationDataset,
        batch_size=a_Config.batch_size,
        shuffle=False,
        num_workers=a_Config.num_workers,
        pin_memory=a_Config.device == "cuda",
    )
    return l_TrainLoader, l_ValidationLoader


def train_model(a_Config: TrainingConfig) -> InterpolationUNet:
    """Train the interpolation network and periodically persist checkpoints."""

    l_Device = torch.device(a_Config.device)
    l_TrainLoader, l_ValidationLoader = create_dataloaders(a_Config)
    l_Model = InterpolationUNet(a_Config.input_channels).to(l_Device)
    l_Criterion = nn.L1Loss()
    l_Optimizer = torch.optim.Adam(l_Model.parameters(), lr=a_Config.learning_rate)

    a_Config.checkpoint_path.parent.mkdir(parents=True, exist_ok=True)
    a_Config.export_path.parent.mkdir(parents=True, exist_ok=True)

    l_BestValidationPsnr = float("-inf")
    l_EpochsWithoutImprovement = 0
    l_BestModelState = None

    for l_Epoch in range(1, a_Config.epochs + 1):
        l_Model.train()
        l_RunningLoss = 0.0
        l_RunningPsnr = 0.0
        l_RunningSsim = 0.0
        for l_BatchIndex, (l_Input, l_Target) in enumerate(l_TrainLoader):
            l_Input = l_Input.to(l_Device)
            l_Target = l_Target.to(l_Device)

            # Forward pass predicts the missing middle frame.
            l_Prediction = l_Model(l_Input)
            l_Loss = l_Criterion(l_Prediction, l_Target)

            l_Optimizer.zero_grad(set_to_none=True)
            l_Loss.backward()
            l_Optimizer.step()

            l_RunningLoss += l_Loss.item()
            l_RunningPsnr += compute_psnr(l_Prediction.detach(), l_Target)
            l_RunningSsim += compute_ssim(l_Prediction.detach(), l_Target)

            if (l_BatchIndex + 1) % 10 == 0:
                print(
                    json.dumps(
                        {
                            "epoch": l_Epoch,
                            "batch": l_BatchIndex + 1,
                            "loss": l_Loss.item(),
                            "psnr": l_RunningPsnr / (l_BatchIndex + 1),
                            "ssim": l_RunningSsim / (l_BatchIndex + 1),
                        }
                    )
                )

        l_EpochLoss = l_RunningLoss / max(len(l_TrainLoader), 1)
        l_EpochPsnr = l_RunningPsnr / max(len(l_TrainLoader), 1)
        l_EpochSsim = l_RunningSsim / max(len(l_TrainLoader), 1)

        l_ValidationLoss = 0.0
        l_ValidationPsnr = 0.0
        l_ValidationSsim = 0.0
        l_Model.eval()
        with torch.no_grad():
            for l_Input, l_Target in l_ValidationLoader:
                l_Input = l_Input.to(l_Device)
                l_Target = l_Target.to(l_Device)
                l_Prediction = l_Model(l_Input)
                l_Loss = l_Criterion(l_Prediction, l_Target)
                l_ValidationLoss += l_Loss.item()
                l_ValidationPsnr += compute_psnr(l_Prediction, l_Target)
                l_ValidationSsim += compute_ssim(l_Prediction, l_Target)

        l_ValidationLoss /= max(len(l_ValidationLoader), 1)
        l_ValidationPsnr /= max(len(l_ValidationLoader), 1)
        l_ValidationSsim /= max(len(l_ValidationLoader), 1)

        print(
            json.dumps(
                {
                    "epoch": l_Epoch,
                    "train_loss": l_EpochLoss,
                    "train_psnr": l_EpochPsnr,
                    "train_ssim": l_EpochSsim,
                    "val_loss": l_ValidationLoss,
                    "val_psnr": l_ValidationPsnr,
                    "val_ssim": l_ValidationSsim,
                }
            )
        )

        l_HasImproved = l_ValidationPsnr > (l_BestValidationPsnr + a_Config.early_stop_min_delta)
        if l_HasImproved:
            l_BestValidationPsnr = l_ValidationPsnr
            l_EpochsWithoutImprovement = 0
            l_BestModelState = {it_Key: it_Value.detach().cpu().clone() for it_Key, it_Value in l_Model.state_dict().items()}
        else:
            l_EpochsWithoutImprovement += 1

        if l_Epoch % a_Config.checkpoint_interval == 0:
            l_CheckpointFile = a_Config.checkpoint_path.with_name(
                f"frame_generator_epoch_{l_Epoch:04d}.pt"
            )
            # Persist checkpoints so long training runs can resume after interruptions.
            torch.save({"model_state": l_Model.state_dict(), "epoch": l_Epoch}, l_CheckpointFile)
            print(f"Saved checkpoint to {l_CheckpointFile}")

        if l_EpochsWithoutImprovement >= a_Config.early_stop_patience:
            print(
                f"Early stopping triggered after {l_EpochsWithoutImprovement} epochs without validation PSNR improvement."
            )
            break

    if l_BestModelState is not None:
        l_Model.load_state_dict(l_BestModelState)

    return l_Model


class NhwcOnnxExportWrapper(nn.Module):
    """Adapter that exposes an NHWC interface around the trained NCHW model."""

    def __init__(self, a_Model: InterpolationUNet) -> None:
        super().__init__()
        self.m_Model = a_Model

    def forward(self, l_Input: torch.Tensor) -> torch.Tensor:  # type: ignore[override]
        # Convert the channels-last tensor produced by the renderer into the channel-first
        # layout expected by the PyTorch model before returning to NHWC for inference output.
        l_ChannelFirst = l_Input.permute(0, 3, 1, 2).contiguous()
        l_ChannelFirstOutput = self.m_Model(l_ChannelFirst)
        return l_ChannelFirstOutput.permute(0, 2, 3, 1)


def export_model(a_Model: InterpolationUNet, a_Config: TrainingConfig) -> None:
    """Export the trained model to ONNX so the renderer can load it."""

    a_Model.eval()
    l_Device = torch.device(a_Config.device)
    if a_Config.input_channels <= 0:
        raise RuntimeError("Input channel count must be discovered during training before exporting ONNX.")

    a_Config.export_path.parent.mkdir(parents=True, exist_ok=True)

    # Wrap the UNet so the exported graph consumes NHWC tensors and surfaces the same layout.
    l_WrappedModel = NhwcOnnxExportWrapper(a_Model).to(l_Device)

    # Dummy tensor encodes the renderer readback layout (batch, height, width, channels).
    l_DummyInput = torch.zeros(
        (1, a_Config.image_size, a_Config.image_size, a_Config.input_channels), device=l_Device
    )
    l_OutputPath = a_Config.export_path

    # Freeze the tensor shapes so CalculateElementCount can validate bindings without fallback logic.
    # Export with a conservative opset so the bundled MSVC/VS2022 runtime (IR 11)
    # can parse the graph. If the runtime is upgraded we can revisit the cap to
    # take advantage of newer operator kernels.
    torch.onnx.export(
        l_WrappedModel,
        l_DummyInput,
        l_OutputPath,
        input_names=["input"],
        output_names=["output"],
        opset_version=a_Config.opset_version,
        dynamic_axes=None,
        do_constant_folding=True,
    )
    try:
        import onnx

        l_Model = onnx.load(l_OutputPath)
        l_Model.ir_version = a_Config.ir_version
        for it_OpSet in l_Model.opset_import:
            it_OpSet.version = a_Config.opset_version

        onnx.save(l_Model, l_OutputPath)
    except ImportError:
        print(
            "onnx package not available; exported model may not carry the explicit IR cap expected by the runtime."
        )

    print(
        f"Exported ONNX model to {l_OutputPath} with opset {a_Config.opset_version} and IR {a_Config.ir_version}"
    )


def main() -> None:
    l_Config = parse_arguments()
    print(json.dumps({"config": l_Config.__dict__}, default=str))
    if l_Config.skip_training:
        if l_Config.input_channels <= 0:
            raise SystemExit("Provide --input-channels when skipping training so export layout is well defined.")

        l_Model = InterpolationUNet(l_Config.input_channels)
    else:
        l_Model = train_model(l_Config)
    export_model(l_Model, l_Config)


if __name__ == "__main__":
    main()