# sonda-hid.ps1 - SOMENTE LEITURA. Lista as interfaces HID do receptor Attack Shark (VID 1D57) com page/usage/tamanhos
# e tenta HidD_GetFeature em todos os report IDs (0..255). Nao envia comandos nem escreve nada no mouse.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File tools\sonda-hid.ps1
# Resultado em 18/11/2026 (X6/R1, 1D57:FA60): nenhuma interface respondeu ao GET_FEATURE sozinho; ver HANDOFF.md, secao 5.1.
$src = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

public static class Sonda {
  [StructLayout(LayoutKind.Sequential)] struct GUID { public uint a; public ushort b, c; public byte d0,d1,d2,d3,d4,d5,d6,d7; }
  [StructLayout(LayoutKind.Sequential)] struct SPDID { public int cb; public GUID g; public int flags; public IntPtr rsv; }
  [StructLayout(LayoutKind.Sequential)] public struct CAPS {
    public ushort Usage, UsagePage, InputReportByteLength, OutputReportByteLength, FeatureReportByteLength;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)] public ushort[] Reserved;
    public ushort NumberLinkCollectionNodes, NumberInputButtonCaps, NumberInputValueCaps, NumberInputDataIndices,
      NumberOutputButtonCaps, NumberOutputValueCaps, NumberOutputDataIndices, NumberFeatureButtonCaps, NumberFeatureValueCaps, NumberFeatureDataIndices; }
  [DllImport("hid.dll")] static extern void HidD_GetHidGuid(out GUID g);
  [DllImport("hid.dll")] static extern bool HidD_GetAttributes(SafeFileHandle h, ref ATTR a);
  [DllImport("hid.dll")] static extern bool HidD_GetPreparsedData(SafeFileHandle h, out IntPtr p);
  [DllImport("hid.dll")] static extern bool HidD_FreePreparsedData(IntPtr p);
  [DllImport("hid.dll")] static extern int HidP_GetCaps(IntPtr p, out CAPS c);
  [DllImport("hid.dll")] static extern bool HidD_GetFeature(SafeFileHandle h, byte[] buf, int len);
  [StructLayout(LayoutKind.Sequential)] struct ATTR { public int Size; public ushort Vid, Pid, Ver; }
  [DllImport("setupapi.dll")] static extern IntPtr SetupDiGetClassDevs(ref GUID g, IntPtr e, IntPtr p, int f);
  [DllImport("setupapi.dll")] static extern bool SetupDiEnumDeviceInterfaces(IntPtr s, IntPtr d, ref GUID g, int i, ref SPDID o);
  [DllImport("setupapi.dll", CharSet = CharSet.Unicode)] static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr s, ref SPDID d, IntPtr det, int sz, out int req, IntPtr di);
  [DllImport("setupapi.dll")] static extern bool SetupDiDestroyDeviceInfoList(IntPtr s);
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  static extern SafeFileHandle CreateFile(string n, uint acc, uint sh, IntPtr sa, uint disp, uint fl, IntPtr t);

  public static string Rodar() {
    var sb = new StringBuilder();
    GUID g; HidD_GetHidGuid(out g);
    IntPtr set = SetupDiGetClassDevs(ref g, IntPtr.Zero, IntPtr.Zero, 0x12); // PRESENT|DEVICEINTERFACE
    for (int i = 0; ; i++) {
      var d = new SPDID(); d.cb = Marshal.SizeOf(typeof(SPDID));
      if (!SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref g, i, ref d)) break;
      int req; SetupDiGetDeviceInterfaceDetail(set, ref d, IntPtr.Zero, 0, out req, IntPtr.Zero);
      IntPtr det = Marshal.AllocHGlobal(req);
      Marshal.WriteInt32(det, IntPtr.Size == 8 ? 8 : 6);
      string path = null;
      if (SetupDiGetDeviceInterfaceDetail(set, ref d, det, req, out req, IntPtr.Zero)) path = Marshal.PtrToStringUni(IntPtr.Add(det, 4));
      Marshal.FreeHGlobal(det);
      if (path == null || path.ToLower().IndexOf("vid_1d57") < 0) continue;
      var h = CreateFile(path, 0xC0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
      if (h.IsInvalid) h = CreateFile(path, 0, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
      if (h.IsInvalid) { sb.AppendLine("nao abriu: " + path); continue; }
      IntPtr pp; HidD_GetPreparsedData(h, out pp);
      CAPS c; HidP_GetCaps(pp, out c);
      sb.AppendFormat("{0}\n  page={1:X4} usage={2:X4} in={3} out={4} feat={5} featBtn={6} featVal={7}\n", path, c.UsagePage, c.Usage, c.InputReportByteLength, c.OutputReportByteLength, c.FeatureReportByteLength, c.NumberFeatureButtonCaps, c.NumberFeatureValueCaps);
      if (c.FeatureReportByteLength > 0) {
        for (int id = 0; id < 256; id++) {
          var buf = new byte[c.FeatureReportByteLength]; buf[0] = (byte)id;
          if (HidD_GetFeature(h, buf, buf.Length)) {
            var hex = new StringBuilder();
            for (int k = 0; k < buf.Length; k++) hex.Append(buf[k].ToString("X2")).Append(' ');
            sb.AppendFormat("  GET_FEATURE id={0:X2} OK: {1}\n", id, hex.ToString().TrimEnd());
          }
        }
      }
      HidD_FreePreparsedData(pp);
      h.Close();
    }
    SetupDiDestroyDeviceInfoList(set);
    return sb.ToString();
  }
}
'@
Add-Type -TypeDefinition $src -Language CSharp
[Sonda]::Rodar()
