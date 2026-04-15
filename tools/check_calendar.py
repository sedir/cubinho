#!/usr/bin/env python3
"""
check_calendar.py — espelha o parsing iCal do firmware Cubinho (calendar_feed.cpp)

Uso:
    python3 tools/check_calendar.py <URL_ICS>
    python3 tools/check_calendar.py <URL_ICS> --date 2026-04-14
    python3 tools/check_calendar.py <URL_ICS> --tz-offset 32400

O script replica fielmente o comportamento do firmware, inclusive a limitação de
não interpretar TZID (trata todos os timestamps sem 'Z' como horário local do
dispositivo). Diferenças são sinalizadas explicitamente.

Parâmetros:
    --date       Simula "hoje" como outra data (YYYY-MM-DD). Padrão: hoje.
    --tz-offset  Offset UTC do dispositivo em segundos. Padrão: 32400 (UTC+9/JST).
    --raw        Exibe o payload iCal bruto recebido.
"""

import sys
import re
import argparse
import urllib.request
import ssl
from datetime import datetime, timezone, timedelta, date

MAX_CALENDAR_EVENTS = 8

# ---------------------------------------------------------------------------
# Normalização da URL (replica normalizeCalendarUrl)
# ---------------------------------------------------------------------------


def normalize_url(url: str) -> str:
    if url.startswith("webcal://"):
        return "https://" + url[9:]
    if url.startswith("webcals://"):
        return "https://" + url[10:]
    return url


# ---------------------------------------------------------------------------
# Parse de linha iCal (replica parseProperty + parseDateValue + unescapeIcsText)
# ---------------------------------------------------------------------------


def parse_property(line: str):
    """Retorna (key, params, value) ou None."""
    colon = line.find(":")
    if colon < 0:
        return None
    before = line[:colon]
    value = line[colon + 1 :]
    semi = before.find(";")
    if semi >= 0:
        key = before[:semi]
        params = before[semi:]
    else:
        key = before
        params = ""
    return key, params, value


def unescape_ics(text: str) -> str:
    """Replica unescapeIcsText: \\n→espaço, \\,→, \\;→; \\\\→\\"""
    out = []
    i = 0
    while i < len(text):
        c = text[i]
        if c == "\\" and i + 1 < len(text):
            n = text[i + 1]
            if n in ("n", "N"):
                out.append(" ")
                i += 2
                continue
            if n in (",", ";", "\\"):
                out.append(n)
                i += 2
                continue
        out.append(c)
        i += 1
    return "".join(out)


def parse_date_value(value: str, device_tz: timezone):
    """
    Replica parseDateValue + toTimestamp do firmware.
    Retorna (dt_aware, all_day, is_utc, warning) ou None se inválido.

    ATENÇÃO: o firmware ignora TZID. Qualquer timestamp sem 'Z' é tratado
    como horário local do dispositivo (device_tz). Esta função replica esse
    comportamento — divergências com TZID são reportadas em 'warning'.
    """
    if len(value) < 8:
        return None

    try:
        year = int(value[0:4])
        month = int(value[4:6])
        day = int(value[6:8])
    except ValueError:
        return None

    all_day = True
    is_utc = False
    warning = None

    if len(value) >= 15 and value[8] == "T":
        try:
            hour = int(value[9:11])
            minute = int(value[11:13])
            second = int(value[13:15])
        except ValueError:
            return None
        all_day = False
        is_utc = value.endswith("Z")
        if is_utc:
            dt = datetime(year, month, day, hour, minute, second, tzinfo=timezone.utc)
        else:
            # Firmware usa mktime (local do dispositivo) — ignora TZID
            dt = datetime(year, month, day, hour, minute, second, tzinfo=device_tz)
    else:
        # Dia inteiro — meia-noite no fuso do dispositivo
        dt = datetime(year, month, day, 0, 0, 0, tzinfo=device_tz)

    return dt, all_day, is_utc, warning


def parse_date_key(value: str) -> int:
    """Replica parseDateKey: inteiro YYYYMMDD."""
    if len(value) < 8:
        return 0
    try:
        return int(value[:8])
    except ValueError:
        return 0


def parse_tz_offset(value: str) -> int:
    """Parse iCal timezone offset: +HHMM or -HHMM → seconds."""
    if len(value) < 5:
        return 0
    sign = -1 if value[0] == "-" else 1
    hours = int(value[1:3])
    mins = int(value[3:5])
    return sign * (hours * 3600 + mins * 60)


# ---------------------------------------------------------------------------
# Unfold de linhas iCal (RFC 5545 §3.1)
# ---------------------------------------------------------------------------


def unfold_lines(text: str) -> list[str]:
    """Replica o mecanismo de pendingLine + continuação do firmware."""
    lines = text.splitlines()
    result = []
    pending = ""
    for raw in lines:
        # Remove CR
        line = raw.rstrip("\r")
        if line and line[0] in (" ", "\t") and pending:
            pending += line[1:]
        else:
            if pending:
                result.append(pending)
            pending = line
    if pending:
        result.append(pending)
    return result


# ---------------------------------------------------------------------------
# Filtro de "overlaps today" (replica maybeStoreEvent)
# ---------------------------------------------------------------------------


def overlaps_today(
    start_dt,
    end_dt,
    all_day,
    today_start,
    tomorrow_start,
    start_key,
    end_key,
    today_key,
):
    """Replica a lógica de maybeStoreEvent."""
    if end_dt is None:
        end_dt = (
            start_dt + timedelta(days=1) if all_day else start_dt + timedelta(minutes=1)
        )

    if end_dt <= start_dt:
        end_dt = (
            start_dt + timedelta(days=1) if all_day else start_dt + timedelta(minutes=1)
        )

    # Verificação por timestamp
    overlap_ts = (start_dt < tomorrow_start) and (end_dt > today_start)

    # Fallback por dateKey (para when timestamps = 0 no firmware)
    if not overlap_ts and start_key > 0 and today_key > 0:
        eff_end_key = end_key if end_key > 0 else start_key
        if all_day:
            if end_key <= 0:
                # Sem DTEND — evento de dia único (RFC 5545 §3.6.1)
                overlap_ts = start_key == today_key
            else:
                overlap_ts = (start_key <= today_key) and (eff_end_key > today_key)
        else:
            overlap_ts = (
                start_key == today_key
                or eff_end_key == today_key
                or (start_key < today_key and eff_end_key > today_key)
            )

    return overlap_ts, end_dt


# ---------------------------------------------------------------------------
# Build summary (replica calendarBuildTodaySummary)
# ---------------------------------------------------------------------------


def build_today_summary(events: list, now_dt: datetime) -> str | None:
    if not events:
        return None

    primary_idx = 0
    for i, e in enumerate(events):
        if e["all_day"]:
            continue
        if e["end_dt"] > now_dt:
            primary_idx = i
            break

    primary = events[primary_idx]
    extra = len(events) - 1  # total - 1 (firmware usa _todayTotalCount - 1)

    if primary["all_day"]:
        if extra > 0:
            return f"Hoje: {primary['title']} +{extra}"
        return f"Hoje: {primary['title']}"

    t = primary["start_dt"].astimezone(now_dt.tzinfo)
    if extra > 0:
        return f"Hoje {t.strftime('%H:%M')} • {primary['title']} +{extra}"
    return f"Hoje {t.strftime('%H:%M')} • {primary['title']}"


# ---------------------------------------------------------------------------
# Parser principal (replica calendarFetchToday)
# ---------------------------------------------------------------------------


def parse_calendar(
    payload: str, device_tz: timezone, sim_date: date, verbose: bool = True
):
    today_start = datetime(
        sim_date.year, sim_date.month, sim_date.day, 0, 0, 0, tzinfo=device_tz
    )
    tomorrow_start = today_start + timedelta(days=1)
    today_key = sim_date.year * 10000 + sim_date.month * 100 + sim_date.day

    if verbose:
        print(f"\n{'='*60}")
        print(f"  Data simulada : {sim_date}  (hoje_key={today_key})")
        print(
            f"  Fuso dispositivo: UTC{int(device_tz.utcoffset(None).total_seconds())//3600:+d}"
        )
        print(f"  today_start   : {today_start.isoformat()}")
        print(f"  tomorrow_start: {tomorrow_start.isoformat()}")
        print(f"{'='*60}\n")

    lines = unfold_lines(payload)
    if verbose:
        print(f"  Linhas após unfold: {len(lines)}\n")

    events_today = []
    events_total = 0
    vevent_count = 0

    # VTIMEZONE map: tzid → offset_seconds
    vtimezones: dict[str, int] = {}
    in_timezone = False
    in_tz_standard = False
    tz_id = ""
    tz_std_offset = 0
    has_tz_std = False

    in_event = False
    title = ""
    start_dt = None
    end_dt = None
    start_raw = ""
    end_raw = ""
    start_params = ""
    end_params = ""
    all_day = False
    has_start = False
    has_end = False
    start_key = 0
    end_key = 0
    tzid_start = None
    tzid_end = None

    def flush_event():
        nonlocal events_total
        if not in_event or not has_start:
            return
        if verbose:
            print(f'  END:VEVENT  "{title}"')
            print(f"    DTSTART raw={start_raw!r} params={start_params!r}")
            print(f"            → {start_dt}  allDay={all_day}")
            if has_end:
                print(f"    DTEND   raw={end_raw!r} params={end_params!r}")
                print(f"            → {end_dt}")
            if tzid_start and tzid_start in vtimezones:
                tz_off = vtimezones[tzid_start]
                eff = timezone(timedelta(seconds=tz_off))
                print(f"    ✓ TZID={tzid_start!r} → offset={tz_off}s (via VTIMEZONE)")
            elif tzid_start:
                print(
                    f"    ⚠ TZID={tzid_start!r} não encontrado em VTIMEZONE"
                    f" — usando fuso do dispositivo {device_tz}"
                )

        ok, end_adj = overlaps_today(
            start_dt,
            end_dt,
            all_day,
            today_start,
            tomorrow_start,
            start_key,
            end_key,
            today_key,
        )

        if verbose:
            if ok:
                print(f"    ✓ ACEITO (overlaps hoje)")
            else:
                print(f"    ✗ REJEITADO (não é hoje)")
                s_local = start_dt.astimezone(device_tz) if start_dt else "?"
                print(f"      start local = {s_local}")
        print()

        if ok:
            events_total += 1
            if len(events_today) < MAX_CALENDAR_EVENTS:
                events_today.append(
                    {
                        "title": title,
                        "start_dt": start_dt,
                        "end_dt": end_adj,
                        "all_day": all_day,
                    }
                )

    for line in lines:
        parsed = parse_property(line)
        if parsed is None:
            continue
        key, params, value = parsed

        if line == "BEGIN:VTIMEZONE":
            in_timezone = True
            tz_id = ""
            tz_std_offset = 0
            has_tz_std = False
            in_tz_standard = False

        elif line == "END:VTIMEZONE":
            if tz_id and has_tz_std:
                vtimezones[tz_id] = tz_std_offset
                if verbose:
                    print(
                        f"  VTIMEZONE: {tz_id!r} → offset={tz_std_offset}s "
                        f"(UTC{tz_std_offset//3600:+d})"
                    )
            in_timezone = False
            in_tz_standard = False

        elif in_timezone:
            if line == "BEGIN:STANDARD":
                in_tz_standard = True
            elif line == "END:STANDARD":
                in_tz_standard = False
            elif key == "TZID":
                tz_id = value
            elif in_tz_standard and key == "TZOFFSETTO":
                tz_std_offset = parse_tz_offset(value)
                has_tz_std = True

        elif line == "BEGIN:VEVENT":
            in_event = True
            vevent_count += 1
            title = ""
            start_dt = None
            end_dt = None
            start_raw = end_raw = ""
            start_params = end_params = ""
            all_day = False
            has_start = has_end = False
            start_key = end_key = 0
            tzid_start = tzid_end = None
            if verbose:
                print(f"  BEGIN:VEVENT #{vevent_count}")

        elif line == "END:VEVENT":
            flush_event()
            in_event = False

        elif in_event and key == "SUMMARY":
            title = unescape_ics(value)[:39]

        elif in_event and key == "DTSTART":
            start_raw = value
            start_params = params
            m = re.search(r"TZID=([^;:]+)", params)
            tzid_start = m.group(1) if m else None
            start_key = parse_date_key(value)
            # Use VTIMEZONE offset if available, otherwise device_tz
            eff_tz = device_tz
            if tzid_start and tzid_start in vtimezones:
                eff_tz = timezone(timedelta(seconds=vtimezones[tzid_start]))
            result = parse_date_value(value, eff_tz)
            if result:
                start_dt, all_day, _, _ = result
                # VALUE=DATE em params → forçar all_day
                if "VALUE=DATE" in params:
                    all_day = True
                has_start = True
            else:
                if verbose:
                    print(f"    ✗ DTSTART parse FALHOU: {value!r}")

        elif in_event and key == "DTEND":
            end_raw = value
            end_params = params
            m = re.search(r"TZID=([^;:]+)", params)
            tzid_end = m.group(1) if m else None
            end_key = parse_date_key(value)
            # Use VTIMEZONE offset if available, otherwise device_tz
            eff_tz = device_tz
            if tzid_end and tzid_end in vtimezones:
                eff_tz = timezone(timedelta(seconds=vtimezones[tzid_end]))
            result = parse_date_value(value, eff_tz)
            if result:
                end_dt, _, _, _ = result
                has_end = True
            else:
                if verbose:
                    print(f"    ✗ DTEND parse FALHOU: {value!r}")

    # Flush caso END:VEVENT seja a última linha sem newline
    if in_event and has_start:
        flush_event()

    # Ordenar por startTs (replica sortTodayEvents)
    events_today.sort(key=lambda e: e["start_dt"])

    return events_today, events_total


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("url", help="URL do feed iCal (.ics / webcal://)")
    parser.add_argument("--date", help="Simular 'hoje' como YYYY-MM-DD (padrão: hoje)")
    parser.add_argument(
        "--tz-offset",
        type=int,
        default=32400,
        help="Offset UTC do dispositivo em segundos (padrão: 32400 = UTC+9)",
    )
    parser.add_argument(
        "--raw", action="store_true", help="Exibir payload bruto recebido"
    )
    args = parser.parse_args()

    url = normalize_url(args.url)
    if url != args.url:
        print(f"  URL normalizada: {args.url!r}  →  {url!r}")

    device_tz = timezone(timedelta(seconds=args.tz_offset))

    if args.date:
        sim_date = date.fromisoformat(args.date)
    else:
        sim_date = datetime.now(device_tz).date()

    # Buscar feed
    print(f"\nFetching {url} ...")
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    try:
        req = urllib.request.Request(
            url,
            headers={
                "User-Agent": "Cubinho/1.0",
                "Cache-Control": "no-cache, no-store",
                "Pragma": "no-cache",
            },
        )
        with urllib.request.urlopen(req, context=ctx, timeout=15) as resp:
            content_length = resp.headers.get("Content-Length")
            raw = resp.read()
            payload = raw.decode("utf-8", errors="replace")
    except Exception as e:
        print(f"ERRO ao buscar URL: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"  Recebido     : {len(payload)} bytes")
    if content_length:
        expected = int(content_length)
        if len(raw) < expected:
            print(f"  ⚠ TRUNCADO   : Content-Length={expected}, recebido={len(raw)}")
        else:
            print(f"  Content-Length: {content_length} bytes (completo)")
    else:
        print(
            f"  Content-Length: ausente (chunked transfer — tamanho só verificável pelo parser)"
        )

    if args.raw:
        print("\n--- PAYLOAD BRUTO ---")
        print(payload)
        print("--- FIM PAYLOAD ---\n")

    # Verificação de integridade (replica nova checagem do firmware)
    if "END:VCALENDAR" not in payload:
        print("  ⚠ END:VCALENDAR ausente — payload provavelmente truncado!")
        print("  O firmware rejeitaria este feed com CAL_STATUS_ERROR.")
    else:
        print(f"  Integridade    : OK (END:VCALENDAR presente)")

    # Parsear
    events_today, events_total = parse_calendar(
        payload, device_tz, sim_date, verbose=True
    )

    # Resultado final
    print("=" * 60)
    print(
        f"  Eventos aceitos hoje : {len(events_today)} (max exibidos: {MAX_CALENDAR_EVENTS})"
    )
    print(f"  Total contabilizado  : {events_total}")

    now_dt = datetime.now(device_tz)
    summary = build_today_summary(events_today, now_dt)

    print()
    if summary:
        print(f'  >>> Texto na Home: "{summary}"')
    else:
        print(f"  >>> Texto na Home: (vazio — nenhum evento hoje)")
        print(f"      Fallback para events.json do SD card.")
    print()


if __name__ == "__main__":
    main()
