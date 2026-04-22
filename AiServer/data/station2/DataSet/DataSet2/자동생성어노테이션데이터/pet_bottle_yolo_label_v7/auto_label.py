"""
Auto-labeling for PET bottle inspection images.
Setup: backlit LCD panel behind bottle; camera in front. Grayscale 1920x1200.

Classes:
  0: cap        (캡)
  1: label      (라벨)
  2: fill_level (충진량 - liquid region inside bottle)
"""
import cv2
import numpy as np
import os
from pathlib import Path


def find_panel(gray):
    """Find backlit panel bbox via row/col mean projection."""
    row_mean = gray.mean(axis=1)
    thresh = row_mean.max() * 0.35
    rows = np.where(row_mean > thresh)[0]
    if len(rows) == 0:
        return None
    py0, py1 = int(rows.min()), int(rows.max())
    strip = gray[py0:py1+1, :]
    col_mean = strip.mean(axis=0)
    col_thresh = col_mean.max() * 0.4
    cols = np.where(col_mean > col_thresh)[0]
    if len(cols) == 0:
        return None
    px0, px1 = int(cols.min()), int(cols.max())
    return px0, py0, px1 - px0 + 1, py1 - py0 + 1


def _find_blobs(gray, roi_y0, roi_y1, roi_x0, roi_x1, thresh, kernel_size, min_area):
    """Generic dark-blob finder in a rectangular ROI."""
    roi = gray[roi_y0:roi_y1, roi_x0:roi_x1]
    if roi.size == 0:
        return []
    _, mask = cv2.threshold(roi, thresh, 255, cv2.THRESH_BINARY_INV)
    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, kernel_size)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)
    num, lbls, stats, _ = cv2.connectedComponentsWithStats(mask, connectivity=8)
    blobs = []
    for i in range(1, num):
        x, y, w, h, area = stats[i]
        if area < min_area:
            continue
        blobs.append({
            'x': roi_x0 + int(x),
            'y': roi_y0 + int(y),
            'w': int(w),
            'h': int(h),
            'area': int(area),
        })
    return blobs


def find_cap(gray, panel):
    """Cap: topmost compact dark blob in the top 45% of the panel, horiz-centered.
    Must be substantial (not a thin strip at the panel's top edge)."""
    px, py, pw, ph = panel
    # Start search a bit INSIDE the panel to skip the top edge shadow
    roi_y0 = py + 10
    roi_y1 = py + int(ph * 0.45)
    roi_x0 = px + int(pw * 0.25)
    roi_x1 = px + int(pw * 0.75)
    blobs = _find_blobs(gray, roi_y0, roi_y1, roi_x0, roi_x1,
                        thresh=95, kernel_size=(5, 5), min_area=5000)
    if not blobs:
        return None
    panel_cx = px + pw // 2
    good = []
    for b in blobs:
        # Cap must be substantial: height > 60, width > 60
        if b['h'] < 60 or b['w'] < 60:
            continue
        # Compact aspect: not a wide thin strip
        aspect = b['w'] / max(b['h'], 1)
        if aspect < 0.5 or aspect > 2.5:
            continue
        # Horizontally centered
        cx = b['x'] + b['w'] / 2
        if abs(cx - panel_cx) > pw * 0.2:
            continue
        good.append(b)
    if not good:
        return None
    good.sort(key=lambda b: b['y'])  # topmost
    b = good[0]
    return (b['x'], b['y'], b['x'] + b['w'], b['y'] + b['h'])


def find_label(gray, panel, cap_bbox):
    """Label: largest dark blob in middle vertical range, below cap, wider than tall-ish."""
    px, py, pw, ph = panel
    if cap_bbox is not None:
        roi_y0 = cap_bbox[3] + 5
    else:
        roi_y0 = py + int(ph * 0.15)
    # Label stays in the upper-middle of the panel, NOT in the bottom
    # 70% of panel height is well above the bottle bottom
    roi_y1 = py + int(ph * 0.75)
    roi_x0 = px + int(pw * 0.20)
    roi_x1 = px + int(pw * 0.80)
    if roi_y1 <= roi_y0:
        return None

    blobs = _find_blobs(gray, roi_y0, roi_y1, roi_x0, roi_x1,
                        thresh=140, kernel_size=(15, 9), min_area=6000)
    if not blobs:
        return None
    # Filter: must have decent width (label is usually 40%+ of panel width at that height)
    panel_cx = px + pw // 2
    good = []
    for b in blobs:
        # Minimum width in pixels: 100
        if b['w'] < 100:
            continue
        # Horizontally centered
        cx = b['x'] + b['w'] / 2
        if abs(cx - panel_cx) > pw * 0.3:
            continue
        good.append(b)
    if not good:
        return None
    # Prefer largest area
    good.sort(key=lambda b: b['area'], reverse=True)
    b = good[0]
    return (b['x'], b['y'], b['x'] + b['w'], b['y'] + b['h'])


def find_bottle_sides_at_y(gray, panel, sample_y):
    """Find bottle left/right edges at a given row.
    The bottle is centered in the panel with a predictable width.
    We estimate from the panel center, then refine using dark edges.
    """
    px, py, pw, ph = panel
    y = max(py, min(py + ph - 1, sample_y))
    # Default estimate: bottle is ~35-40% of panel width, centered
    panel_cx = px + pw // 2
    est_half = int(pw * 0.20)
    est_left = panel_cx - est_half
    est_right = panel_cx + est_half

    # Refine by looking for dark edge within ±20% of estimate
    row = gray[y, :].astype(np.int32)
    # Bottle edges are local minima darker than interior.
    # Search window around estimate
    win = int(pw * 0.12)
    # Left edge: look in [est_left - win, est_left + win]
    l0 = max(0, est_left - win)
    l1 = min(len(row) - 1, est_left + win)
    left_region = row[l0:l1]
    # Find the LAST position (rightmost) in this region where brightness is low
    # going from the center outward, the edge is where brightness drops
    # Actually simpler: find argmin in the region
    if len(left_region) > 0:
        left_edge = l0 + int(np.argmin(left_region))
    else:
        left_edge = est_left
    r0 = max(0, est_right - win)
    r1 = min(len(row) - 1, est_right + win)
    right_region = row[r0:r1]
    if len(right_region) > 0:
        right_edge = r0 + int(np.argmin(right_region))
    else:
        right_edge = est_right
    return left_edge, right_edge


def tighten_cap_bbox(gray, bbox):
    """Tighten cap bbox to only the solid cap material.
    Cap is very dark (< 60) with high density (>60% per row/col).
    Strict thresholds avoid the transparent neck/ring area below the cap."""
    x0, y0, x1, y1 = bbox
    h, w = gray.shape
    # Allow slight expansion down in case cap extends beyond initial bbox
    ey1 = min(h, y1 + 15)
    region = gray[y0:ey1, x0:x1]
    mask = (region < 60).astype(np.uint8)
    if mask.size == 0:
        return bbox
    row_density = mask.sum(axis=1) / max(x1 - x0, 1)
    cap_rows = np.where(row_density > 0.6)[0]
    if len(cap_rows) < 3:
        # fallback: looser threshold if strict one gives nothing
        mask = (region < 75).astype(np.uint8)
        row_density = mask.sum(axis=1) / max(x1 - x0, 1)
        cap_rows = np.where(row_density > 0.5)[0]
        if len(cap_rows) < 3:
            return bbox
    # Keep only contiguous top run (avoid picking up neck ring below)
    # Start from first cap row, walk while rows are consecutive-ish
    kept = [int(cap_rows[0])]
    for r in cap_rows[1:]:
        if r - kept[-1] <= 3:
            kept.append(int(r))
        else:
            break
    new_y0 = y0 + kept[0]
    new_y1 = y0 + kept[-1] + 1

    # Tighten columns
    sub = gray[new_y0:new_y1, x0:x1]
    sub_mask = (sub < 60).astype(np.uint8)
    col_density = sub_mask.sum(axis=0) / max(new_y1 - new_y0, 1)
    cap_cols = np.where(col_density > 0.6)[0]
    if len(cap_cols) < 3:
        return (x0, new_y0, x1, new_y1)
    new_x0 = x0 + int(cap_cols.min())
    new_x1 = x0 + int(cap_cols.max()) + 1
    return (new_x0, new_y0, new_x1, new_y1)


def tighten_label_bbox(gray, bbox):
    """Tighten label bbox using ink-density with a sliding window.
    Real label rows have sustained high ink density (print / graphics).
    Neck area above label may have sparse dark refraction edges, but
    they don't sustain across a window of rows.
    We use a 10-row sliding average of density > 0.15 to find real label."""
    x0, y0, x1, y1 = bbox
    region = gray[y0:y1, x0:x1]
    if region.size == 0:
        return bbox
    # Ink pixels (label print) are < 100
    ink = (region < 100).astype(np.float32)
    if ink.sum() < 50:
        return bbox

    row_density = ink.sum(axis=1) / max(x1 - x0, 1)
    # Smooth with 10-row window (requires sustained density)
    win = 10
    kernel = np.ones(win, dtype=np.float32) / win
    smoothed = np.convolve(row_density, kernel, mode='same')
    # Require sustained density > 0.15 (sparse dark rows won't pass)
    label_rows = np.where(smoothed > 0.15)[0]
    if len(label_rows) < 10:
        # Fallback: looser threshold
        label_rows = np.where(smoothed > 0.10)[0]
        if len(label_rows) < 5:
            return bbox

    # Longest contiguous run (tight gap tolerance)
    runs = []
    start = int(label_rows[0])
    prev = int(label_rows[0])
    for r in label_rows[1:]:
        if r - prev > 3:
            runs.append((start, prev))
            start = int(r)
        prev = int(r)
    runs.append((start, prev))
    runs.sort(key=lambda r: r[1] - r[0], reverse=True)
    rstart, rend = runs[0]
    new_y0 = y0 + rstart
    new_y1 = y0 + rend + 1

    # Column tightening (same approach)
    sub = gray[new_y0:new_y1, x0:x1]
    sub_ink = (sub < 100).astype(np.float32)
    col_density = sub_ink.sum(axis=0) / max(new_y1 - new_y0, 1)
    col_smoothed = np.convolve(col_density, np.ones(8, dtype=np.float32)/8, mode='same')
    label_cols = np.where(col_smoothed > 0.08)[0]
    if len(label_cols) < 5:
        return (x0, new_y0, x1, new_y1)
    runs = []
    start = int(label_cols[0])
    prev = int(label_cols[0])
    for c in label_cols[1:]:
        if c - prev > 8:
            runs.append((start, prev))
            start = int(c)
        prev = int(c)
    runs.append((start, prev))
    runs.sort(key=lambda r: r[1] - r[0], reverse=True)
    cstart, cend = runs[0]
    new_x0 = x0 + cstart
    new_x1 = x0 + cend + 1
    return (new_x0, new_y0, new_x1, new_y1)


def derive_fill_xrange(panel, label_bbox, cap_bbox):
    """Derive fill_level x-range.
    Prefer label bbox (tightest match to bottle width at body).
    Fall back to cap-based or panel-based estimate.
    """
    px, py, pw, ph = panel
    panel_cx = px + pw // 2
    if label_bbox is not None:
        lx0, _, lx1, _ = label_bbox
        # Bottle body might be slightly wider than label (a few pixels each side)
        margin = max(3, int((lx1 - lx0) * 0.03))
        return lx0 - margin, lx1 + margin
    if cap_bbox is not None:
        cx0, _, cx1, _ = cap_bbox
        cap_w = cx1 - cx0
        cap_center = (cx0 + cx1) // 2
        # Bottle body typically ~1.7x cap width
        half = int(cap_w * 0.85)
        return cap_center - half, cap_center + half
    half = int(pw * 0.15)
    return panel_cx - half, panel_cx + half


def find_bottle_bottom(gray, panel, fx0, fx1):
    """Return a consistent y-coordinate for fill_level bottom.
    
    Design decision: fill_level's BOTTOM edge carries no information
    (bottle bottom is always at the same physical location). The only
    informative dimension is the TOP edge (meniscus / fill height).
    
    Using a DYNAMIC per-image bottle-bottom detection introduces noise
    that competes with the actual fill-state signal. Instead, we use
    a consistent reference: the panel bottom. Panel detection varies
    only ±2px across images (very stable), so this gives us a clean
    constant lower edge that the YOLO model can ignore.
    
    The bottle's physical bottom extends ~20-30px below panel bottom on
    its support. We include a small fixed offset to cover most of it
    while keeping consistency.
    """
    _, _ = gray.shape[:2]  # unused but signature kept
    _ = fx0; _ = fx1       # unused
    px, py, pw, ph = panel
    # Panel bottom + small fixed offset for bottle extension below panel
    return py + ph + 20


def _detect_meniscus_step(gray, y0, y1, cx0, cx1, require_bright_above=True):
    """Detect a liquid meniscus as a STEP function in brightness.
    A meniscus has UNIFORMLY bright (air) above and less-bright (water) below.
    This distinguishes it from plastic ridges (thin dips) and normal bottles
    (where water below label has refraction gradient but no clean air region).
    
    Returns (y_coord, strength) of strongest step, or (None, 0).
    """
    if y1 - y0 < 30:
        return None, 0.0
    strip = gray[y0:y1, cx0:cx1]
    if strip.size == 0:
        return None, 0.0
    profile = strip.mean(axis=1).astype(np.float32)
    profile = cv2.GaussianBlur(profile.reshape(-1, 1), (5, 1), 0).flatten()
    n = len(profile)
    half_win = 15

    step = np.zeros(n, dtype=np.float32)
    for i in range(half_win, n - half_win):
        above = profile[i - half_win:i].mean()
        below = profile[i + 1:i + 1 + half_win].mean()
        step[i] = above - below
    if step.max() < 15.0:
        return None, 0.0
    peak_idx = int(np.argmax(step))
    peak_val = float(step[peak_idx])

    if require_bright_above:
        # For underfill: require "above" region to be GENUINELY BRIGHT AIR
        # - mean brightness very high (>210, true air not refracted water)
        # - low std (uniform, not varying due to bottle features)
        above_region = profile[max(0, peak_idx - half_win):peak_idx]
        above_mean = float(above_region.mean())
        above_std = float(above_region.std())
        below_region = profile[peak_idx + 1:peak_idx + 1 + half_win]
        below_mean = float(below_region.mean())
        # Strict criteria for real underfill meniscus
        if above_mean < 210:
            return None, 0.0
        if above_std > 15:  # air should be uniform
            return None, 0.0
        if peak_val < 25:   # need substantial step
            return None, 0.0
        # The step should be "clean": below must be meaningfully darker
        if below_mean > above_mean - 20:
            return None, 0.0

    return y0 + peak_idx, peak_val


def find_liquid_top(gray, panel, cap_bbox, label_bbox):
    """Find the y-coordinate of the liquid surface (fill_level box TOP).

    Strategy:
    1) If label exists, check BELOW the label for a step-function meniscus.
       Found = underfill (liquid surface visible below label).
    2) If nothing below, check ABOVE the label (neck) for meniscus.
       Found = normal fill with visible surface in neck.
    3) If label exists and neither detection worked: use label_top
       (liquid hidden by label - conservative estimate).
    4) No label: use just-below-cap position.
    """
    px, py, pw, ph = panel
    # Wider strip for more reliable averaging
    cx0 = px + int(pw * 0.35)
    cx1 = px + int(pw * 0.65)

    # Step 1: Below the label (underfill detection) - requires bright "air" above
    if label_bbox is not None:
        by0 = label_bbox[3] + 15
        by1 = py + ph - 20
        menisc_y, strength = _detect_meniscus_step(
            gray, by0, by1, cx0, cx1, require_bright_above=True)
        if menisc_y is not None and strength > 25.0:
            return menisc_y

    # Step 2: Above the label (normal fill in neck area)
    if label_bbox is not None:
        # Define where to start searching upward from above label
        if cap_bbox is not None:
            ny0 = cap_bbox[3] + 15
        else:
            # No cap: start from top of panel
            ny0 = py + 20
        ny1 = label_bbox[1] - 5
        if ny1 - ny0 > 20:
            menisc_y, strength = _detect_meniscus_step(
                gray, ny0, ny1, cx0, cx1, require_bright_above=False)
            if menisc_y is not None and strength > 15.0:
                return menisc_y
    elif cap_bbox is not None:
        # No label, has cap
        ny0 = cap_bbox[3] + 15
        ny1 = min(py + ph - 30, cap_bbox[3] + int(ph * 0.5))
        menisc_y, strength = _detect_meniscus_step(
            gray, ny0, ny1, cx0, cx1, require_bright_above=False)
        if menisc_y is not None and strength > 15.0:
            return menisc_y

    # Fallback: liquid hidden by label → use label top (conservative)
    if label_bbox is not None:
        return label_bbox[1]
    # No label: just below cap
    if cap_bbox is not None:
        return cap_bbox[3] + 10
    return py + int(ph * 0.25)


def label_image(img_path, expect=None):
    img = cv2.imread(str(img_path), cv2.IMREAD_GRAYSCALE)
    if img is None:
        return None, []
    expect = expect or {}
    has_cap = expect.get("has_cap", True)
    has_label = expect.get("has_label", True)

    panel = find_panel(img)
    if panel is None:
        return img, []

    results = []
    cap_bbox = find_cap(img, panel) if has_cap else None
    if cap_bbox is not None:
        cap_bbox = tighten_cap_bbox(img, cap_bbox)
        results.append((0, cap_bbox))

    label_bbox = find_label(img, panel, cap_bbox) if has_label else None
    if label_bbox is not None:
        label_bbox = tighten_label_bbox(img, label_bbox)
        results.append((1, label_bbox))

    fy0 = find_liquid_top(img, panel, cap_bbox, label_bbox)
    px, py, pw, ph = panel
    # x-range: use label/cap as reliable width reference
    fx0, fx1 = derive_fill_xrange(panel, label_bbox, cap_bbox)
    # Consistent bottom reference (panel bottom + small offset)
    fy1 = find_bottle_bottom(img, panel, fx0, fx1)
    if fx1 > fx0 + 60 and fy1 > fy0 + 60:
        results.append((2, (fx0, fy0, fx1, fy1)))

    return img, results


CLASSES = ["cap", "label", "fill_level"]


def to_yolo(bbox, img_w, img_h):
    x0, y0, x1, y1 = bbox
    cx = (x0 + x1) / 2 / img_w
    cy = (y0 + y1) / 2 / img_h
    w = (x1 - x0) / img_w
    h = (y1 - y0) / img_h
    return cx, cy, w, h


def draw_preview(gray, results, save_path):
    img = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
    colors = [(0, 255, 255), (0, 255, 0), (255, 150, 0)]
    for cid, bbox in results:
        x0, y0, x1, y1 = map(int, bbox)
        cv2.rectangle(img, (x0, y0), (x1, y1), colors[cid], 3)
        cv2.putText(img, CLASSES[cid], (x0, max(20, y0 - 10)),
                    cv2.FONT_HERSHEY_SIMPLEX, 1.0, colors[cid], 2)
    cv2.imwrite(str(save_path), img)


if __name__ == "__main__":
    test_imgs = [
        ("img_data/2_normal/Image__2026-04-20__15-25-25.bmp", {"has_cap": True, "has_label": True}),
        ("img_data/2_abnormal/캡없음/Image__2026-04-20__17-03-06.bmp", {"has_cap": False, "has_label": True}),
        ("img_data/2_abnormal/라벨없음/Image__2026-04-20__16-54-42.bmp", {"has_cap": True, "has_label": False}),
        ("img_data/2_abnormal/충전량/Image__2026-04-20__17-06-30.bmp", {"has_cap": True, "has_label": True}),
        ("img_data/2_abnormal/캡미세하게열림/Image__2026-04-20__17-00-07.bmp", {"has_cap": True, "has_label": True}),
        ("img_data/2_abnormal/라벨각도다름/Image__2026-04-20__16-56-55.bmp", {"has_cap": True, "has_label": True}),
    ]
    os.makedirs("test_out", exist_ok=True)
    for path, expect in test_imgs:
        gray, results = label_image(path, expect=expect)
        name = Path(path).parent.name + "__" + Path(path).stem
        draw_preview(gray, results, f"test_out/{name}.jpg")
        print(f"{name}: {len(results)} detections, panel={find_panel(gray)}")
        for cid, bbox in results:
            print(f"  {CLASSES[cid]}: {bbox}")
