# Makes test cover images in testdata/_img (System.Drawing; colors and shapes only)
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Drawing; using System.Drawing.Imaging;
public static class CoverMaker {
  public static void Make(string path, int w, int h, string c1, string c2, bool jpeg) {
    using (Bitmap bmp = new Bitmap(w, h)) {
      using (Graphics g = Graphics.FromImage(bmp)) {
        g.Clear(Color.FromName(c1));
        using (Brush b2 = new SolidBrush(Color.FromName(c2))) g.FillEllipse(b2, (int)(w * 0.15), (int)(h * 0.15), (int)(w * 0.7), (int)(h * 0.7));
        using (Pen pen = new Pen(Color.White, Math.Max(2, w / 40))) { g.DrawRectangle(pen, 2, 2, w - 5, h - 5); g.DrawLine(pen, 0, 0, w, h); }
      }
      bmp.Save(path, jpeg ? ImageFormat.Jpeg : ImageFormat.Png);
    }
  }
}
'@
$out = Join-Path (Split-Path -Parent $PSScriptRoot) 'testdata\_img'
New-Item -ItemType Directory -Force -Path $out | Out-Null
[CoverMaker]::Make("$out\red.jpg", 300, 300, 'Crimson', 'Gold', $true)
[CoverMaker]::Make("$out\green.png", 200, 200, 'ForestGreen', 'White', $false)
[CoverMaker]::Make("$out\blue.jpg", 600, 400, 'RoyalBlue', 'Orange', $true)
[CoverMaker]::Make("$out\small.jpg", 120, 120, 'Purple', 'Yellow', $true)
[CoverMaker]::Make("$out\big.png", 2400, 2400, 'Teal', 'Pink', $false)
Write-Host "covers in $out"
