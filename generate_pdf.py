import os
from reportlab.lib.pagesizes import letter
from reportlab.lib import colors
from reportlab.lib.units import inch
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, HRFlowable
)
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.enums import TA_CENTER, TA_LEFT, TA_RIGHT, TA_JUSTIFY
from reportlab.graphics.shapes import Drawing, Rect, String, Line

def build_pdf(filename="GANTT_CHART.pdf"):
    doc = SimpleDocTemplate(
        filename,
        pagesize=letter,
        leftMargin=0.5 * inch,
        rightMargin=0.5 * inch,
        topMargin=0.5 * inch,
        bottomMargin=0.5 * inch
    )

    styles = getSampleStyleSheet()
    
    # Custom styles
    title_style = ParagraphStyle(
        'DocTitle',
        parent=styles['Heading1'],
        fontName='Helvetica-Bold',
        fontSize=18,
        leading=22,
        textColor=colors.HexColor('#1A365D'),
        alignment=TA_LEFT,
        spaceAfter=4
    )
    
    subtitle_style = ParagraphStyle(
        'DocSubTitle',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=9.5,
        leading=13,
        textColor=colors.HexColor('#4A5568'),
        spaceAfter=10
    )

    heading2_style = ParagraphStyle(
        'SectionHeading',
        parent=styles['Heading2'],
        fontName='Helvetica-Bold',
        fontSize=13,
        leading=16,
        textColor=colors.HexColor('#2B6CB0'),
        spaceBefore=12,
        spaceAfter=6
    )

    body_style = ParagraphStyle(
        'BodyDark',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=8.5,
        leading=12,
        textColor=colors.HexColor('#2D3748'),
        spaceAfter=4
    )

    bold_body_style = ParagraphStyle(
        'BoldBody',
        parent=body_style,
        fontName='Helvetica-Bold',
        textColor=colors.HexColor('#1A365D')
    )

    table_header_style = ParagraphStyle(
        'TableHeader',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=8,
        leading=10,
        textColor=colors.white,
        alignment=TA_CENTER
    )

    table_cell_style = ParagraphStyle(
        'TableCell',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=7.5,
        leading=10,
        textColor=colors.HexColor('#2D3748')
    )

    table_cell_center = ParagraphStyle(
        'TableCellCenter',
        parent=table_cell_style,
        alignment=TA_CENTER
    )

    story = []

    # Title Banner
    story.append(Paragraph("STM32 Simulated Linux: 6-Week Gantt Chart & Project Roadmap", title_style))
    story.append(Paragraph("Master schedule detailing the POSIX compatibility layer development on FreeRTOS in QEMU (June 8, 2026 – July 19, 2026).", subtitle_style))
    story.append(HRFlowable(width="100%", thickness=1, color=colors.HexColor('#CBD5E0'), spaceAfter=8))

    # Section 1: Visual Gantt Chart Representation
    story.append(Paragraph("1. Gantt Chart Timeline Diagram", heading2_style))

    # Visual Gantt data structure
    gantt_data = [
        ("Phase 1: Setup & Threading", [
            ("Env & QEMU Setup", "06/08", "06/14", 0, 6, colors.HexColor('#3182CE')),
            ("POSIX Thread Shim", "06/11", "06/16", 3, 5, colors.HexColor('#4299E1')),
        ]),
        ("Phase 2: Design & Feasibility", [
            ("Feasibility Study", "06/15", "06/23", 7, 8, colors.HexColor('#DD6B20')),
            ("Feasibility Report", "06/22", "06/26", 14, 4, colors.HexColor('#ED8936')),
        ]),
        ("Phase 3: Documentation", [
            ("MacOS Build Fixes", "06/29", "07/03", 21, 4, colors.HexColor('#38A169')),
            ("System Overview", "07/02", "07/06", 24, 4, colors.HexColor('#48BB78')),
        ]),
        ("Phase 4: Core POSIX Shims", [
            ("Thread Lifecycle", "07/06", "07/12", 28, 6, colors.HexColor('#805AD5')),
            ("Mutex & Semaphore Sync", "07/09", "07/15", 31, 6, colors.HexColor('#9F7AEA')),
            ("Timing Primitives", "07/13", "07/15", 35, 2, colors.HexColor('#B794F4')),
        ]),
        ("Phase 5: LwIP Stack & Driver", [
            ("LwIP Integration", "07/13", "07/16", 35, 3, colors.HexColor('#D69E2E')),
            ("SMSC9118 Driver & IRQ", "07/15", "07/17", 37, 2, colors.HexColor('#ECC94B')),
        ]),
        ("Phase 6: Sockets & Web App", [
            ("POSIX Socket Layer", "07/16", "07/18", 38, 2, colors.HexColor('#E53E3E')),
            ("HTTP Web Server Demo", "07/17", "07/19", 39, 2, colors.HexColor('#F56565')),
        ])
    ]

    # Draw Gantt Chart Box
    chart_width = 7.5 * inch
    row_height = 18
    num_tasks = sum(len(tasks) for _, tasks in gantt_data) + len(gantt_data)
    chart_height = num_tasks * row_height + 30
    
    d = Drawing(chart_width, chart_height)
    
    # Background
    d.add(Rect(0, 0, chart_width, chart_height, fillColor=colors.HexColor('#F8FAFC'), strokeColor=colors.HexColor('#E2E8F0'), strokeWidth=1))
    
    # Header Time Axis
    left_margin = 165
    timeline_width = chart_width - left_margin - 10
    days_total = 42 # 6 weeks
    day_px = timeline_width / days_total

    # Header background
    d.add(Rect(0, chart_height - 22, chart_width, 22, fillColor=colors.HexColor('#EDF2F7'), strokeColor=colors.HexColor('#CBD5E0')))
    d.add(String(8, chart_height - 15, "Task / Module Name", fontName="Helvetica-Bold", fontSize=8.5, fillColor=colors.HexColor('#2D3748')))
    
    weeks = [
        ("W1 (06/08)", 0),
        ("W2 (06/15)", 7),
        ("W3 (06/22)", 14),
        ("W4 (06/29)", 21),
        ("W5 (07/06)", 28),
        ("W6 (07/13)", 35),
    ]

    for w_name, day_offset in weeks:
        x_pos = left_margin + day_offset * day_px
        d.add(Line(x_pos, 0, x_pos, chart_height - 22, strokeColor=colors.HexColor('#E2E8F0'), strokeWidth=1))
        d.add(String(x_pos + 3, chart_height - 15, w_name, fontName="Helvetica-Bold", fontSize=7.5, fillColor=colors.HexColor('#4A5568')))

    # Populate Tasks & Bars
    curr_y = chart_height - 36
    for section_name, tasks in gantt_data:
        # Section header line
        d.add(Rect(0, curr_y - 2, chart_width, 15, fillColor=colors.HexColor('#E2E8F0'), strokeColor=None))
        d.add(String(8, curr_y + 2, section_name, fontName="Helvetica-Bold", fontSize=8, fillColor=colors.HexColor('#1A365D')))
        curr_y -= row_height

        for task_name, start_str, end_str, start_day, duration_days, bar_color in tasks:
            # Task Label
            d.add(String(12, curr_y + 3, task_name, fontName="Helvetica", fontSize=7.5, fillColor=colors.HexColor('#2D3748')))
            
            # Task Bar
            bar_x = left_margin + start_day * day_px
            bar_w = max(duration_days * day_px, 10)
            d.add(Rect(bar_x, curr_y + 1, bar_w, 11, fillColor=bar_color, strokeColor=None, rx=2, ry=2))
            
            # Date text next to bar
            d.add(String(bar_x + bar_w + 3, curr_y + 3, f"{start_str}-{end_str}", fontName="Helvetica", fontSize=6, fillColor=colors.HexColor('#718096')))
            
            curr_y -= row_height

    story.append(d)
    story.append(Spacer(1, 10))

    # Section 2: Master Schedule Table
    story.append(Paragraph("2. Master Schedule Breakdown", heading2_style))

    table_data = [
        [
            Paragraph("ID", table_header_style),
            Paragraph("Phase", table_header_style),
            Paragraph("Full Task Name", table_header_style),
            Paragraph("Start", table_header_style),
            Paragraph("End", table_header_style),
            Paragraph("Days", table_header_style),
            Paragraph("Key Output / Deliverable", table_header_style)
        ]
    ]

    tasks_raw = [
        ("t1", "Phase 1", "Initial QEMU & FreeRTOS Setup", "06/08", "06/14", "7", "QEMU UART redirection in main.c"),
        ("t2", "Phase 1", "Basic POSIX Thread Translation Shim", "06/11", "06/16", "6", "pthread_create mapping to xTaskCreate"),
        ("t3", "Phase 2", "Research & Feasibility Study", "06/15", "06/23", "9", "Cortex-M3 RAM budget & POSIX evaluation"),
        ("t4", "Phase 2", "Feasibility Report & Heatmap", "06/22", "06/26", "5", "Feasibility report & heatmap creation"),
        ("t5", "Phase 3", "MacOS Build Fixes & Debugging", "06/29", "07/03", "5", "Toolchain build fix & Makefile updates"),
        ("t6", "Phase 3", "System Overview & Architecture", "07/02", "07/06", "5", "Architectural guide SYSTEM_OVERVIEW.md"),
        ("t7", "Phase 4", "Thread Lifecycle (join, exit, detach)", "07/06", "07/12", "7", "Thread registry & lifecycle in main_blinky.c"),
        ("t8", "Phase 4", "Mutex & Semaphore Synchronization", "07/09", "07/15", "7", "pthread_mutex_t & counting sem_t"),
        ("t9", "Phase 4", "Timing Primitives (sleep, usleep)", "07/13", "07/15", "3", "Delay mapping to vTaskDelay ticks"),
        ("t10", "Phase 5", "LwIP TCP/IP Stack Integration", "07/13", "07/16", "4", "LwIP OS layer adaptation in sys_arch.c"),
        ("t11", "Phase 5", "SMSC9118 Driver & NVIC IRQ", "07/15", "07/17", "3", "Ethernet driver in ethernetif.c"),
        ("t12", "Phase 6", "POSIX Socket Shim Wrapper Layers", "07/16", "07/18", "3", "BSD Socket APIs (socket, bind, listen)"),
        ("t13", "Phase 6", "HTTP Web Server Demo App", "07/17", "07/19", "3", "Simulated POSIX Web Server on port 80/8080"),
    ]

    for tid, ph, tname, sdate, edate, dur, out in tasks_raw:
        table_data.append([
            Paragraph(f"<b>{tid}</b>", table_cell_center),
            Paragraph(ph, table_cell_style),
            Paragraph(f"<b>{tname}</b>", table_cell_style),
            Paragraph(sdate, table_cell_center),
            Paragraph(edate, table_cell_center),
            Paragraph(dur, table_cell_center),
            Paragraph(out, table_cell_style),
        ])

    col_widths = [0.35 * inch, 0.75 * inch, 1.8 * inch, 0.55 * inch, 0.55 * inch, 0.45 * inch, 3.05 * inch]
    t = Table(table_data, colWidths=col_widths, repeatRows=1)
    t.setStyle(TableStyle([
        ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#1A365D')),
        ('TEXTCOLOR', (0, 0), (-1, 0), colors.white),
        ('ALIGN', (0, 0), (-1, -1), 'LEFT'),
        ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
        ('BOTTOMPADDING', (0, 0), (-1, -1), 3),
        ('TOPPADDING', (0, 0), (-1, -1), 3),
        ('GRID', (0, 0), (-1, -1), 0.5, colors.HexColor('#CBD5E0')),
        ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.white, colors.HexColor('#F8FAFC')]),
    ]))

    story.append(t)
    story.append(Spacer(1, 10))

    # Section 3: Detailed Week-by-Week Breakdown
    story.append(Paragraph("3. Detailed Week-by-Week Breakdown", heading2_style))

    weeks_detail = [
        ("Week 1 (June 8 - June 14, 2026): Initial Bootstrap & Threading Shim", [
            "Imported the FreeRTOS real-time kernel and MPS2 AN385 Cortex-M3 template configuration.",
            "Overrode stdout bindings in main.c to pipe console prints directly to QEMU UART0.",
            "Implemented initial pthread_create translation to FreeRTOS xTaskCreate task calls."
        ]),
        ("Week 2 (June 15 - June 21, 2026): Architectural Design & Research", [
            "Analyzed memory constraints and RAM budget (70-80 KB limit) of the target board.",
            "Mapped priority scaling and stack depth allocations needed for simulating Linux threads.",
            "Formulated synchronization primitive strategy without requiring Unix host headers."
        ]),
        ("Week 3 (June 22 - June 28, 2026): Feasibility Report & Heatmap Creation", [
            "Documented feasibility research results and API translation mappings.",
            "Created methodology evaluation heatmap scoring POSIX Shimming vs. Manual Rewriting.",
            "Prepared build instructions and repository setup in README.md."
        ]),
        ("Week 4 (June 29 - July 5, 2026): Tooling & Documentation Setup", [
            "Resolved type-casting constraints inside pthread_create to fix toolchain build warnings.",
            "Updated build Makefile to ensure seamless cross-compilation across Linux and macOS.",
            "Authored SYSTEM_OVERVIEW.md detailing architectural layout and execution pathways."
        ]),
        ("Week 5 (July 6 - July 12, 2026): POSIX Shim Expansion", [
            "Implemented thread lifecycle APIs: pthread_join, pthread_exit, pthread_detach, pthread_self.",
            "Mapped pthread_mutex_t to FreeRTOS mutexes and built custom sem_t counting semaphores.",
            "Integrated timing delay wrappers (sleep, usleep) mapped to FreeRTOS scheduler ticks."
        ]),
        ("Week 6 (July 13 - July 19, 2026): Networking Stack & Sockets Web Server", [
            "Integrated LwIP TCP/IP stack with custom memory configuration tuned in lwipopts.h.",
            "Built sys_arch.c OS adaptation layer and SMSC9118 Ethernet driver in ethernetif.c.",
            "Mapped BSD socket APIs and built HTTP Web Server daemon listening on port 80/8080."
        ])
    ]

    for w_title, bullets in weeks_detail:
        story.append(Paragraph(w_title, bold_body_style))
        for bullet in bullets:
            story.append(Paragraph(f"• {bullet}", body_style))
        story.append(Spacer(1, 3))

    doc.build(story)
    print("PDF generated successfully:", filename)

if __name__ == "__main__":
    build_pdf()
