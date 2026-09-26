import csv

rows = list(csv.DictReader(open("results/e1/e1_summary.csv")))
for scene in ["occlusion", "hdr"]:
    print(f"=== {scene} ===")
    for r in rows:
        if r["scene"] != scene:
            continue
        def f(k):
            v = r.get(k, "")
            return float(v) if v else float("nan")
        print("%-30s mse=%.4g med=%.4g std=%.4g psnr=%.2f t=%.4g rays=%.4g" % (
            r["name"], f("mse_linear"), f("mse_median"), f("mse_std"),
            f("psnr_linear"), f("time_render_mean"), f("rays_per_pixel")))
