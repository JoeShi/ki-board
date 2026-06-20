import {
  AbsoluteFill,
  useCurrentFrame,
  useVideoConfig,
  interpolate,
  Easing,
} from "remotion";

const stack = [
  { category: "框架", value: "Arduino + PlatformIO (pioarduino)" },
  { category: "USB HID", value: "ESP32 内置 USB.h + USBHIDKeyboard" },
  { category: "屏幕驱动", value: "Arduino_GFX (ST7735 / GC9D01 / ST7789)" },
  { category: "状态同步", value: "Kiro CLI Hook → Python JSONL → USB CDC" },
  { category: "BLE 备用", value: "HijelHID_BLEKeyboard (NimBLE)" },
];

export const TechStack: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  const titleOpacity = interpolate(frame, [0, fps * 0.4], [0, 1], {
    extrapolateRight: "clamp",
  });

  return (
    <AbsoluteFill
      style={{
        justifyContent: "center",
        alignItems: "center",
        background:
          "radial-gradient(ellipse at 50% 80%, #1a1a2e 0%, #0a0a0f 70%)",
        padding: 80,
      }}
    >
      <div style={{ width: "100%", maxWidth: 900 }}>
        <h2
          style={{
            fontSize: 52,
            fontWeight: 700,
            color: "#a5b4fc",
            opacity: titleOpacity,
            fontFamily: "system-ui, sans-serif",
            margin: "0 0 40px 0",
          }}
        >
          🛠 技术栈
        </h2>

        <div style={{ display: "flex", flexDirection: "column", gap: 18 }}>
          {stack.map((item, i) => {
            const delay = fps * 0.4 + i * fps * 0.35;
            const opacity = interpolate(
              frame,
              [delay, delay + fps * 0.4],
              [0, 1],
              { extrapolateLeft: "clamp", extrapolateRight: "clamp" }
            );
            const x = interpolate(
              frame,
              [delay, delay + fps * 0.4],
              [30, 0],
              {
                extrapolateLeft: "clamp",
                extrapolateRight: "clamp",
                easing: Easing.out(Easing.cubic),
              }
            );

            return (
              <div
                key={item.category}
                style={{
                  display: "flex",
                  alignItems: "center",
                  gap: 24,
                  opacity,
                  transform: `translateX(${x}px)`,
                }}
              >
                <div
                  style={{
                    width: 140,
                    fontSize: 18,
                    fontWeight: 600,
                    color: "#6366f1",
                    fontFamily: "system-ui, sans-serif",
                    textAlign: "right",
                    flexShrink: 0,
                  }}
                >
                  {item.category}
                </div>
                <div
                  style={{
                    height: 2,
                    width: 24,
                    background: "rgba(165,180,252,0.3)",
                    flexShrink: 0,
                  }}
                />
                <div
                  style={{
                    fontSize: 24,
                    color: "#e2e8f0",
                    fontFamily: "system-ui, sans-serif",
                  }}
                >
                  {item.value}
                </div>
              </div>
            );
          })}
        </div>
      </div>
    </AbsoluteFill>
  );
};
