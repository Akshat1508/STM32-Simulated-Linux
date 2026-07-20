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
    story.append(Paragraph("Master schedule detailing the POSIX compatibility layer development on FreeRTOS in QEMU (01-06-2026 – 12-07-2026).", subtitle_style))
    story.append(HRFlowable(width="100%", thickness=1, color=colors.HexColor('#CBD5E0'), spaceAfter=8))

    # Section 1: Visual Gantt Chart Representation
    story.append(Paragraph("1. Gantt Chart Timeline Diagram", heading2_style))

    # Visual Gantt data structure shifted back by 1 week (01/06 - 12/07)
    gantt_data = [
        ("Phase 1: Design & Feasibility", [
            ("Literature Research", "01/06", "05/06", 0, 4, colors.HexColor('#C05621')),
            ("Feasibility Study", "04/06", "09/06", 3, 5, colors.HexColor('#DD6B20')),
            ("Feasibility Report & Heatmap", "08/06", "12/06", 7, 4, colors.HexColor('#ED8936')),
        ]),
        ("Phase 2: QEMU & FreeRTOS Setup", [
            ("Initial QEMU & FreeRTOS Setup", "11/06", "17/06", 10, 6, colors.HexColor('#3182CE')),
            ("Memory Layout & UART Redir", "15/06", "19/06", 14, 4, colors.HexColor('#4299E1')),
        ]),
        ("Phase 3: Thread Shim & Docs", [
            ("Basic POSIX Thread Shim", "22/06", "26/06", 21, 4, colors.HexColor('#38A169')),
            ("Build Fixes & Debugging", "23/06", "27/06", 22, 4, colors.HexColor('#48BB78')),
            ("System Overview Doc", "25/06", "28/06", 24, 3, colors.HexColor('#68D391')),
        ]),
        ("Phase 4: Core POSIX Shims", [
            ("Thread Lifecycle", "29/06", "03/07", 28, 4, colors.HexColor('#805AD5')),
            ("Mutex & Semaphore Sync", "01/07", "05/07", 30, 4, colors.HexColor('#9F7AEA')),
            ("Timing Primitives", "03/07", "05/07", 32, 2, colors.HexColor('#B794F4')),
        ]),
        ("Phase 5: LwIP Stack & Driver", [
            ("LwIP Stack Integration", "06/07", "09/07", 35, 3, colors.HexColor('#D69E2E')),
            ("SMSC9118 Driver & IRQ", "08/07", "10/07", 37, 2, colors.HexColor('#ECC94B')),
        ]),
        ("Phase 6: Sockets & Web App", [
            ("POSIX Socket Layer", "09/07", "11/07", 38, 2, colors.HexColor('#E53E3E')),
            ("HTTP Web Server Demo", "10/07", "12/07", 39, 2, colors.HexColor('#F56565')),
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
    
    # Header Time Axis (Weeks 1 to 6 in DD/MM format shifted back 1 week)
    left_margin = 165
    timeline_width = chart_width - left_margin - 10
    days_total = 42
    day_px = timeline_width / days_total

    # Header background
    d.add(Rect(0, chart_height - 22, chart_width, 22, fillColor=colors.HexColor('#EDF2F7'), strokeColor=colors.HexColor('#CBD5E0')))
    d.add(String(8, chart_height - 15, "Task / Module Name", fontName="Helvetica-Bold", fontSize=8.5, fillColor=colors.HexColor('#2D3748')))
    
    weeks = [
        ("W1 (01/06)", 0),
        ("W2 (08/06)", 7),
        ("W3 (15/06)", 14),
        ("W4 (22/06)", 21),
        ("W5 (29/06)", 28),
        ("W6 (06/07)", 35),
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
            Paragraph("Key Output / Deliverable", table_header_style)
        ]
    ]

    tasks_raw = [
        ("t0", "Phase 1", "Literature Research", "Survey of POSIX RTOS shimming & Cortex-M memory bounds"),
        ("t1", "Phase 1", "Research & Feasibility Study", "Cortex-M3 RAM budget & POSIX evaluation"),
        ("t2", "Phase 1", "Feasibility Report & Heatmap", "Feasibility report & heatmap creation"),
        ("t3", "Phase 2", "Initial QEMU & FreeRTOS Setup", "FreeRTOS Kernel & QEMU MPS2 target setup"),
        ("t4", "Phase 2", "Memory Layout & UART Redir", "Linker script mps2_m3.ld & main.c UART init"),
        ("t5", "Phase 3", "Basic POSIX Thread Translation Shim", "pthread_create mapping to xTaskCreate"),
        ("t6", "Phase 3", "Build Fixes & Debugging", "Toolchain cross-compilation fix"),
        ("t7", "Phase 3", "System Overview & Architecture", "Architectural guide SYSTEM_OVERVIEW.md"),
        ("t8", "Phase 4", "Thread Lifecycle (join, exit, detach)", "Thread registry & lifecycle in main_blinky.c"),
        ("t9", "Phase 4", "Mutex & Semaphore Synchronization", "pthread_mutex_t & counting sem_t"),
        ("t10", "Phase 4", "Timing Primitives (sleep, usleep)", "Delay mapping to vTaskDelay ticks"),
        ("t11", "Phase 5", "LwIP TCP/IP Stack Integration", "LwIP OS layer adaptation in sys_arch.c"),
        ("t12", "Phase 5", "SMSC9118 Driver & NVIC IRQ", "Ethernet driver in ethernetif.c"),
        ("t13", "Phase 6", "POSIX Socket Shim Wrapper Layers", "BSD Socket APIs (socket, bind, listen)"),
        ("t14", "Phase 6", "HTTP Web Server Demo App", "Simulated POSIX Web Server on port 80/8080"),
    ]

    for tid, ph, tname, out in tasks_raw:
        table_data.append([
            Paragraph(f"<b>{tid}</b>", table_cell_center),
            Paragraph(ph, table_cell_style),
            Paragraph(f"<b>{tname}</b>", table_cell_style),
            Paragraph(out, table_cell_style),
        ])

    col_widths = [0.4 * inch, 1.1 * inch, 2.2 * inch, 3.8 * inch]
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
        ("Phase 1: Design, Heatmap & Feasibility Report (01-06 to 12-06-2026)", [
            "Performed literature research on POSIX API shimming over real-time operating systems.",
            "Evaluated memory constraints and RAM budget (~70-80 KB) of the target board.",
            "Mapped API conversion strategies, priority scaling, and stack depth allocations for simulating Linux threads.",
            "Created methodology evaluation heatmap comparing POSIX Shimming vs. Manual Rewriting and Full Emulation.",
            "Published initial project guidelines and repository setup documentation in README.md."
        ]),
        ("Phase 2: QEMU Setup & FreeRTOS Installation (11-06 to 19-06-2026)", [
            "Imported the FreeRTOS real-time kernel and template configuration for Cortex-M3 MPS2 AN385 platform.",
            "Configured linker script mps2_m3.ld defining memory layouts for FLASH (4096K) and SRAM (8192K).",
            "Overrode stdout bindings in main.c to pipe console prints directly to QEMU UART0 registers."
        ]),
        ("Phase 3: POSIX Thread Shim, Build Fixes & System Overview (22-06 to 28-06-2026)", [
            "Designed lightweight pthread_create translation mapping POSIX requests directly to FreeRTOS xTaskCreate.",
            "Fixed type-casting constraints inside pthread_create and updated Makefile compiler flags.",
            "Created SYSTEM_OVERVIEW.md detailing architectural layout and execution pathways."
        ]),
        ("Phase 4: Core POSIX Shim Layers (29-06 to 05-07-2026)", [
            "Implemented thread lifecycle APIs in main_blinky.c (pthread_join, exit, detach, self).",
            "Mapped pthread_mutex_t to FreeRTOS mutexes and built custom sem_t counting semaphores.",
            "Integrated timing delay wrappers (sleep, usleep) mapped to FreeRTOS scheduler ticks."
        ]),
        ("Phase 5: LwIP Stack & Driver (06-07 to 10-07-2026)", [
            "Integrated LwIP TCP/IP stack with custom memory configuration tuned in lwipopts.h.",
            "Built sys_arch.c OS adaptation layer and SMSC9118 Ethernet driver in ethernetif.c.",
            "Configured interrupt service routines (NVIC IRQ 13) to process incoming packet queues asynchronously."
        ]),
        ("Phase 6: Sockets & Web Application (09-07 to 12-07-2026)", [
            "Mapped standard Linux BSD sockets (socket, bind, listen, accept, read, write, close) to LwIP.",
            "Developed a simulated POSIX HTTP Web Server daemon inside main_blinky.c listening on port 80/8080."
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
