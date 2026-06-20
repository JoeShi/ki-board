import {
  AbsoluteFill,
  useCurrentFrame,
  useVideoConfig,
  interpolate,
  Easing,
} from "remotion";

export const Outro: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  const opacity = interpolate(frame, [0, fps * 0.8], [0, 1], {
    extrapolateRight: "clamp",
    easing: Easing.out(Easing.cubic),
  });

  const scale = interpolate(frame, [0, fps * 0.8], [0.95, 1], {
    extrapolateRight: "clamp",
    easing: Easing.out(Easing.cubic),
  });

  return (
    <AbsoluteFill
      style={{
        justifyContent: "center",
        alignItems: "center",
        background:
          "radial-gradient(ellipse at center, #1a1a2e 0%, #0a0a0f 70%)",
      }}
    >
      <div
        style={{
          display: "flex",
          flexDirection: "column",
          alignItems: "center",
          gap: 20,
          opacity,
          transform: `scale(${scale})`,
        }}
      >
        <h2
          style={{
            fontSize: 56,
            fontWeight: 700,
            color: "#fff",
            fontFamily: "system-ui, sans-serif",
            margin: 0,
          }}
        >
          Vibe Coding, Physically.
        </h2>
        <p
          style={{
            fontSize: 24,
            color: "#94a3b8",
            fontFamily: "system-ui, sans-serif",
            margin: 0,
          }}
        >
          github.com/anthropics/vibe-coding-keyboard
        </p>
      </div>
    </AbsoluteFill>
  );
};
