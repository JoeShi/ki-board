import { Composition } from "remotion";
import { KiroKeyboard } from "./KiroKeyboard";

// 4 + 6 + 8 + 5 + 3 = 26 seconds total
export const RemotionRoot: React.FC = () => {
  return (
    <Composition
      id="KiroKeyboard"
      component={KiroKeyboard}
      durationInFrames={26 * 30}
      fps={30}
      width={1920}
      height={1080}
    />
  );
};
