import { mkdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import sharp from "sharp";

const sourceDirectory = fileURLToPath(new URL("../src/img/", import.meta.url));
const outputDirectory = fileURLToPath(new URL("../src/img/generated/", import.meta.url));
const logos = ["gatas-dark"];

await mkdir(outputDirectory, { recursive: true });

await Promise.all(
  logos.map((logo) =>
    sharp(`${sourceDirectory}/${logo}.png`)
      .resize({ width: 320, withoutEnlargement: true })
      .webp({ quality: 76, alphaQuality: 90, effort: 6 })
      .toFile(`${outputDirectory}/${logo}.webp`),
  ),
);
