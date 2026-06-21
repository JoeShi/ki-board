import {
  AbsoluteFill,
  useCurrentFrame,
  useVideoConfig,
  interpolate,
  Easing,
} from "remotion";

const features = [
  {
    key: "左键",
    action: "切换 Agent",
    shortcut: "⌘ + ]",
    color: "#f472b6",
  },
  {
    key: "中键",
    action: "语音输入 / 发送",
    shortcut: "双击 Control / Enter",
    color: "#34d399",
  },
  {
    key: "右键",
    action: "取消 / 打断",
    shortcut: "ESC / Backspace",
    color: "#60a5fa",
  },
];

const FeatureRow: React.FC<{
  feature: (typeof features)[number];
  index: number;
}> = ({ feature, index }) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  const delay = fps + index * fps * 0.8;
  const progress = interpolate(frame, [delay, delay + fps * 0.6], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
    easing: Easing.out(Easing.cubic),
  });

  const scale = interpolate(progress, [0, 1], [0.9, 1]);

  return (
    <div
      style={{
        display: "flex",
        alignItems: "center",
        gap: 32,
        opacity: progress,
        transform: `scale(${scale})`,
      }}
    >
      {/* Key button */}
      <div
        style={{
          width: 100,
          height: 100,
          borderRadius: 16,
          background: `linear-gradient(135deg, ${feature.color}33, ${feature.color}11)`,
          border: `2px solid ${feature.color}`,
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
          fontSize: 22,
          fontWeight: 700,
          color: feature.color,
          fontFamily: "system-ui, sans-serif",
          flexShrink: 0,
        }}
      >
        {feature.key}
      </div>

      {/* Action description */}
      <div style={{ flex: 1 }}>
        <div
          style={{
            fontSize: 30,
            fontWeight: 600,
            color: "#fff",
            fontFamily: "system-ui, sans-serif",
          }}
        >
          {feature.action}
        </div>
        <div
          style={{
            fontSize: 20,
            color: "#64748b",
            fontFamily: "monospace",
            marginTop: 6,
          }}
        >
          {feature.shortcut}
        </div>
      </div>
    </div>
  );
};

export const Features: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  const titleOpacity = interpolate(frame, [0, fps * 0.5], [0, 1], {
    extrapolateRight: "clamp",
  });

  // Agent status display animation
  const statusDelay = fps * 4;
  const statusOpacity = interpolate(
    frame,
    [statusDelay, statusDelay + fps],
    [0, 1],
    { extrapolateLeft: "clamp", extrapolateRight: "clamp" }
  );

  return (
    <AbsoluteFill
      style={{
        background:
          "radial-gradient(ellipse at 70% 50%, #1a1a2e 0%, #0a0a0f 70%)",
        padding: 80,
        display: "flex",
        flexDirection: "row",
      }}
    >
      {/* Left: key actions */}
      <div style={{ flex: 1, display: "flex", flexDirection: "column", justifyContent: "center", gap: 36 }}>
        <h2
          style={{
            fontSize: 48,
            fontWeight: 700,
            color: "#a5b4fc",
            opacity: titleOpacity,
            fontFamily: "system-ui, sans-serif",
            margin: "0 0 20px 0",
          }}
        >
          🎹 按键行为
        </h2>
        {features.map((f, i) => (
          <FeatureRow key={f.key} feature={f} index={i} />
        ))}
      </div>

      {/* Right: agent status mock */}
      <div
        style={{
          flex: 1,
          display: "flex",
          flexDirection: "column",
          justifyContent: "center",
          alignItems: "center",
          opacity: statusOpacity,
        }}
      >
        <h3
          style={{
            fontSize: 28,
            color: "#a5b4fc",
            fontFamily: "system-ui, sans-serif",
            marginBottom: 20,
          }}
        >
          矩形屏 · Agent 状态
        </h3>
        <div
          style={{
            display: "grid",
            gridTemplateColumns: "1fr 1fr",
            gap: 12,
            width: 320,
          }}
        >
          {["Planner", "Coder", "Reviewer", "Runner"].map((name, i) => (
            <div
              key={name}
              style={{
                background: "rgba(255,255,255,0.05)",
                border: "1px solid rgba(165,180,252,0.3)",
                borderRadius: 12,
                padding: "14px 16px",
                textAlign: "center",
              }}
            >
              <div
                style={{
                  fontSize: 14,
                  color: "#94a3b8",
                  fontFamily: "system-ui, sans-serif",
                }}
              >
                {name}
              </div>
              <div
                style={{
                  fontSize: 18,
                  fontWeight: 600,
                  color: i === 1 ? "#34d399" : "#64748b",
                  fontFamily: "system-ui, sans-serif",
                  marginTop: 4,
                }}
              >
                {i === 1 ? "Running" : "Idle"}
              </div>
            </div>
          ))}
        </div>
      </div>
    </AbsoluteFill>
  );
};
