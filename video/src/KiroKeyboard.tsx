import {
  AbsoluteFill,
  Sequence,
  useCurrentFrame,
  useVideoConfig,
  interpolate,
  Easing,
} from "remotion";
import { Intro } from "./scenes/Intro";
import { Hardware } from "./scenes/Hardware";
import { Features } from "./scenes/Features";
import { TechStack } from "./scenes/TechStack";
import { Outro } from "./scenes/Outro";

export const KiroKeyboard: React.FC = () => {
  const { fps } = useVideoConfig();

  // Scene durations in seconds
  const introDur = 4 * fps;
  const hardwareDur = 6 * fps;
  const featuresDur = 8 * fps;
  const techDur = 5 * fps;
  const outroDur = 3 * fps;

  let offset = 0;

  return (
    <AbsoluteFill style={{ backgroundColor: "#0a0a0f" }}>
      <Sequence from={offset} durationInFrames={introDur}>
        <Intro />
      </Sequence>

      <Sequence from={(offset += introDur)} durationInFrames={hardwareDur}>
        <Hardware />
      </Sequence>

      <Sequence from={(offset += hardwareDur)} durationInFrames={featuresDur}>
        <Features />
      </Sequence>

      <Sequence from={(offset += featuresDur)} durationInFrames={techDur}>
        <TechStack />
      </Sequence>

      <Sequence from={(offset += techDur)} durationInFrames={outroDur}>
        <Outro />
      </Sequence>
    </AbsoluteFill>
  );
};
