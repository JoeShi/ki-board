import {
  AbsoluteFill,
  useCurrentFrame,
  useVideoConfig,
  interpolate,
  Easing,
} from "remotion";

export const Intro: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  const titleOpacity = interpolate(frame, [0, fps], [0, 1], {
    extrapolateRight: "clamp",
    easing: Easing.out(Easing.cubic),
  });

  const titleY = interpolate(frame, [0, fps], [40, 0], {
    extrapolateRight: "clamp",
    easing: Easing.out(Easing.cubic),
  });

  const subtitleOpacity = interpolate(frame, [fps * 0.8, fps * 1.8], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });

  const glowScale = interpolate(frame, [0, fps * 2], [0.8, 1.2], {
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
      {/* Glow background */}
      <div
        style={{
          position: "absolute",
          width: 400,
          height: 400,
          borderRadius: "50%",
          background:
            "radial-gradient(circle, rgba(99,102,241,0.3) 0%, transparent 70%)",
          transform: `scale(${glowScale})`,
        }}
      />

      <div
        style={{
          display: "flex",
          flexDirection: "column",
          alignItems: "center",
          gap: 24,
        }}
      >
        <h1
          style={{
            fontSize: 80,
            fontWeight: 800,
            color: "#ffffff",
            opacity: titleOpacity,
            transform: `translateY(${titleY}px)`,
            fontFamily: "system-ui, sans-serif",
            margin: 0,
            letterSpacing: -2,
          }}
        >
          Kiro 快捷键盘
        </h1>
        <p
          style={{
            fontSize: 32,
            color: "#a5b4fc",
            opacity: subtitleOpacity,
            fontFamily: "system-ui, sans-serif",
            margin: 0,
          }}
        >
          5 屏桌面控制器 · Agentic Coding 工作流
        </p>
      </div>
    </AbsoluteFill>
  );
};
