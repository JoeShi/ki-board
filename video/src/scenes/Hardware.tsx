import {
  AbsoluteFill,
  Sequence,
  useCurrentFrame,
  useVideoConfig,
  interpolate,
  Easing,
} from "remotion";

const components = [
  { label: "ESP32-S3", desc: "双核 240MHz · 16MB Flash · 8MB PSRAM", icon: "🧠" },
  { label: "3× 屏幕按键", desc: "0.85\" ST7735 · 128×128 · 机械轴", icon: "🎹" },
  { label: "圆形 LCD", desc: "0.71\" GC9D01 · 160×160 · Agent 表情", icon: "🟣" },
  { label: "矩形 LCD", desc: "1.47\" ST7789 · 172×320 · 状态总览", icon: "📺" },
  { label: "定制 PCB", desc: "ESP32_5SCREEN_V0.1 · 双 SPI 总线", icon: "🔌" },
];

const ComponentCard: React.FC<{
  item: (typeof components)[number];
  index: number;
}> = ({ item, index }) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  const delay = index * (fps * 0.4);
  const opacity = interpolate(frame, [delay, delay + fps * 0.5], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
    easing: Easing.out(Easing.cubic),
  });

  const x = interpolate(frame, [delay, delay + fps * 0.5], [60, 0], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
    easing: Easing.out(Easing.cubic),
  });

  return (
    <div
      style={{
        display: "flex",
        alignItems: "center",
        gap: 20,
        opacity,
        transform: `translateX(${x}px)`,
        background: "rgba(255,255,255,0.05)",
        borderRadius: 16,
        padding: "16px 28px",
        border: "1px solid rgba(165,180,252,0.2)",
      }}
    >
      <span style={{ fontSize: 42 }}>{item.icon}</span>
      <div>
        <div
          style={{
            fontSize: 28,
            fontWeight: 700,
            color: "#fff",
            fontFamily: "system-ui, sans-serif",
          }}
        >
          {item.label}
        </div>
        <div
          style={{
            fontSize: 18,
            color: "#94a3b8",
            fontFamily: "system-ui, sans-serif",
            marginTop: 4,
          }}
        >
          {item.desc}
        </div>
      </div>
    </div>
  );
};

export const Hardware: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  const titleOpacity = interpolate(frame, [0, fps * 0.5], [0, 1], {
    extrapolateRight: "clamp",
  });

  return (
    <AbsoluteFill
      style={{
        justifyContent: "center",
        alignItems: "center",
        background:
          "radial-gradient(ellipse at 30% 50%, #1a1a2e 0%, #0a0a0f 70%)",
      }}
    >
      <div
        style={{
          display: "flex",
          flexDirection: "column",
          gap: 20,
          padding: 80,
          width: "100%",
        }}
      >
        <h2
          style={{
            fontSize: 52,
            fontWeight: 700,
            color: "#a5b4fc",
            opacity: titleOpacity,
            fontFamily: "system-ui, sans-serif",
            margin: "0 0 24px 0",
          }}
        >
          🧩 硬件配置
        </h2>
        {components.map((item, i) => (
          <ComponentCard key={item.label} item={item} index={i} />
        ))}
      </div>
    </AbsoluteFill>
  );
};
