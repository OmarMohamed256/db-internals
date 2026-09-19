#!/usr/bin/env python3
"""
Pragmatic DBMS Implementation Roadmap PDF Generator
Focused on High-ROI Conceptual Depth & Eliminating Low-Level Toil
Synchronized with CMU 15-445/645 and CMU 15-721
"""

import os
import sys
from reportlab.lib.pagesizes import letter
from reportlab.lib import colors
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, KeepTogether, HRFlowable
)
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.enums import TA_CENTER, TA_LEFT, TA_RIGHT, TA_JUSTIFY
from reportlab.pdfgen import canvas

class NumberedCanvas(canvas.Canvas):
    """
    Two-pass canvas to calculate total page count and draw running headers/footers.
    """
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._saved_page_states = []

    def showPage(self):
        self._saved_page_states.append(dict(self.__dict__))
        self._startPage()

    def save(self):
        num_pages = len(self._saved_page_states)
        for state in self._saved_page_states:
            self.__dict__.update(state)
            self.draw_page_decorations(num_pages)
            super().showPage()
        super().save()

    def draw_page_decorations(self, page_count):
        if self._pageNumber == 1:
            # Decorative top bar on cover page
            self.saveState()
            self.setFillColor(colors.HexColor("#1A365D"))
            self.rect(0, 786, 612, 6, fill=True, stroke=False)
            self.setFillColor(colors.HexColor("#2B6CB0"))
            self.rect(0, 780, 612, 6, fill=True, stroke=False)
            self.setFillColor(colors.HexColor("#D69E2E"))
            self.rect(0, 776, 612, 4, fill=True, stroke=False)
            self.restoreState()
            return

        self.saveState()
        self.setFont("Helvetica-Bold", 8)
        self.setFillColor(colors.HexColor("#2B6CB0"))
        
        # Running Header
        self.drawString(42, 756, "THE PRAGMATIC DBMS IMPLEMENTATION ROADMAP")
        self.setFont("Helvetica", 8)
        self.setFillColor(colors.HexColor("#718096"))
        self.drawString(275, 756, "•  High-ROI Database Internals Curriculum")
        self.drawRightString(612 - 42, 756, "db-internals Engine")
        
        # Header Rule
        self.setStrokeColor(colors.HexColor("#CBD5E0"))
        self.setLineWidth(0.75)
        self.line(42, 748, 612 - 42, 748)

        # Footer Rule
        self.line(42, 38, 612 - 42, 38)
        
        # Running Footer
        self.setFont("Helvetica", 7.5)
        self.drawString(42, 26, "CMU 15-445 / 15-721 Pragmatic Learning Track — High Depth, Zero Toil")
        page_str = f"Page {self._pageNumber} of {page_count}"
        self.drawRightString(612 - 42, 26, page_str)
        self.restoreState()

def build_pdf(filename):
    doc = SimpleDocTemplate(
        filename,
        pagesize=letter,
        leftMargin=42,
        rightMargin=42,
        topMargin=48,
        bottomMargin=48
    )

    PAGE_WIDTH = 528

    styles = getSampleStyleSheet()
    
    # Custom Palette
    C_PRIMARY = colors.HexColor("#1A365D")   # Deep Navy
    C_SECONDARY = colors.HexColor("#2B6CB0") # Slate Blue
    C_ACCENT = colors.HexColor("#D69E2E")    # Warm Amber
    C_DARK = colors.HexColor("#2D3748")      # Charcoal Body
    C_MUTED = colors.HexColor("#718096")     # Slate Gray
    C_BG_LIGHT = colors.HexColor("#F7FAFC")  # Light Gray
    C_BORDER = colors.HexColor("#E2E8F0")    # Soft Border
    C_SUCCESS = colors.HexColor("#22543D")   # Forest Green
    C_SUCCESS_BG = colors.HexColor("#F0FFF4")
    C_WARNING_BG = colors.HexColor("#FFFAF0")
    C_INFO_BG = colors.HexColor("#EBF8FF")
    C_DANGER_BG = colors.HexColor("#FFF5F5")
    C_DANGER = colors.HexColor("#C53030")

    title_style = ParagraphStyle(
        'CoverTitle',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=23,
        leading=27,
        textColor=C_PRIMARY,
        spaceAfter=6
    )

    subtitle_style = ParagraphStyle(
        'CoverSubtitle',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=11,
        leading=15,
        textColor=C_SECONDARY,
        spaceAfter=10
    )

    meta_style = ParagraphStyle(
        'CoverMeta',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=8,
        leading=11.5,
        textColor=C_MUTED
    )

    h1_style = ParagraphStyle(
        'SectionH1',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=13,
        leading=17,
        textColor=C_PRIMARY,
        spaceBefore=12,
        spaceAfter=6,
        keepWithNext=True
    )

    h2_style = ParagraphStyle(
        'SectionH2',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=10.5,
        leading=14,
        textColor=C_SECONDARY,
        spaceBefore=8,
        spaceAfter=4,
        keepWithNext=True
    )

    h3_style = ParagraphStyle(
        'SectionH3',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=9,
        leading=12,
        textColor=C_DARK,
        spaceBefore=5,
        spaceAfter=3,
        keepWithNext=True
    )

    body_style = ParagraphStyle(
        'CustomBody',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=8,
        leading=11,
        textColor=C_DARK,
        spaceAfter=4
    )

    body_bold = ParagraphStyle(
        'CustomBodyBold',
        parent=body_style,
        fontName='Helvetica-Bold'
    )

    bullet_style = ParagraphStyle(
        'CustomBullet',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=7.5,
        leading=10.5,
        textColor=C_DARK,
        leftIndent=10,
        firstLineIndent=-7,
        spaceAfter=2
    )

    tbl_header = ParagraphStyle(
        'TableHeader',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=7.5,
        leading=9.5,
        textColor=colors.white,
        alignment=TA_LEFT
    )

    tbl_cell = ParagraphStyle(
        'TableCell',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=7,
        leading=9,
        textColor=C_DARK
    )

    tbl_cell_bold = ParagraphStyle(
        'TableCellBold',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=7,
        leading=9,
        textColor=C_PRIMARY
    )

    tbl_cell_mono = ParagraphStyle(
        'TableCellMono',
        parent=styles['Normal'],
        fontName='Courier',
        fontSize=6.5,
        leading=8.5,
        textColor=C_DARK
    )

    tbl_cell_badge_done = ParagraphStyle(
        'BadgeDone',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=6.5,
        leading=8,
        textColor=colors.HexColor("#22543D"),
        alignment=TA_CENTER
    )

    tbl_cell_badge_next = ParagraphStyle(
        'BadgeNext',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=6.5,
        leading=8,
        textColor=colors.HexColor("#2B6CB0"),
        alignment=TA_CENTER
    )

    tbl_cell_badge_skip = ParagraphStyle(
        'BadgeSkip',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=6.5,
        leading=8,
        textColor=colors.HexColor("#C53030"),
        alignment=TA_CENTER
    )

    # Helper Components
    def make_callout(text, title=None, c_type="info"):
        if c_type == "info":
            bg, border_c = C_INFO_BG, C_SECONDARY
        elif c_type == "success":
            bg, border_c = C_SUCCESS_BG, colors.HexColor("#38A169")
        elif c_type == "warning":
            bg, border_c = C_WARNING_BG, C_ACCENT
        else: # danger
            bg, border_c = C_DANGER_BG, C_DANGER

        content = []
        if title:
            content.append(Paragraph(f"<b>{title}</b>", ParagraphStyle('CalloutTitle', parent=body_bold, textColor=border_c)))
            content.append(Spacer(1, 2))
        content.append(Paragraph(text, body_style))
        tbl = Table([[content]], colWidths=[PAGE_WIDTH])
        tbl.setStyle(TableStyle([
            ('BACKGROUND', (0,0), (-1,-1), bg),
            ('BOX', (0,0), (-1,-1), 0.5, border_c),
            ('LINEBEFORE', (0,0), (0,0), 3, border_c),
            ('TOPPADDING', (0,0), (-1,-1), 4),
            ('BOTTOMPADDING', (0,0), (-1,-1), 4),
            ('LEFTPADDING', (0,0), (-1,-1), 7),
            ('RIGHTPADDING', (0,0), (-1,-1), 7),
        ]))
        return tbl

    def make_section_banner(title, subtitle=None):
        content = [Paragraph(title.upper(), ParagraphStyle('BannerTitle', fontName='Helvetica-Bold', fontSize=11, leading=14, textColor=colors.white))]
        if subtitle:
            content.append(Paragraph(subtitle, ParagraphStyle('BannerSub', fontName='Helvetica', fontSize=7.5, leading=9.5, textColor=colors.HexColor("#E2E8F0"))))
        tbl = Table([[content]], colWidths=[PAGE_WIDTH])
        tbl.setStyle(TableStyle([
            ('BACKGROUND', (0,0), (-1,-1), C_PRIMARY),
            ('TOPPADDING', (0,0), (-1,-1), 5),
            ('BOTTOMPADDING', (0,0), (-1,-1), 5),
            ('LEFTPADDING', (0,0), (-1,-1), 8),
            ('RIGHTPADDING', (0,0), (-1,-1), 8),
        ]))
        return tbl

    def make_sprint_card(sprint_id, title, lectures, focus_goal, build_items, skip_items, why_matters, test_plan, deliverable, status="CORE PRIORITY"):
        # Top banner
        status_bg = C_SUCCESS if status == "COMPLETED" else (C_SECONDARY if "CORE" in status else C_ACCENT)
        header_p = Paragraph(f"<b>SPRINT {sprint_id}: {title.upper()}</b>", ParagraphStyle('STitle', fontName='Helvetica-Bold', fontSize=8.5, leading=10.5, textColor=colors.white))
        badge_p = Paragraph(f"<b>{status}</b>", ParagraphStyle('SBadge', fontName='Helvetica-Bold', fontSize=6.5, leading=8, textColor=colors.white, alignment=TA_RIGHT))
        
        top_bar = Table([[header_p, badge_p]], colWidths=[PAGE_WIDTH - 120, 120])
        top_bar.setStyle(TableStyle([
            ('BACKGROUND', (0,0), (-1,-1), C_PRIMARY),
            ('TOPPADDING', (0,0), (-1,-1), 3),
            ('BOTTOMPADDING', (0,0), (-1,-1), 3),
            ('LEFTPADDING', (0,0), (-1,-1), 6),
            ('RIGHTPADDING', (0,0), (-1,-1), 6),
            ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
        ]))

        rows = []
        # Meta info
        meta_text = f"<b>Prerequisite Lectures:</b> <font color='#1A365D'>{lectures}</font><br/><b>Primary Learning Goal:</b> {focus_goal}"
        rows.append([Paragraph(meta_text, body_style)])

        # What to build (Gold Core)
        build_text = "<b><font color='#22543D'>[BUILD - High ROI Core]:</font></b><br/>" + "".join([f"• {b}<br/>" for b in build_items])
        rows.append([Paragraph(build_text, body_style)])

        # What to skip / simplify (Toil Reduction)
        if skip_items:
            skip_text = "<b><font color='#C53030'>[SKIP / SIMPLIFY - Low ROI Toil]:</font></b><br/>" + "".join([f"• {s}<br/>" for s in skip_items])
            rows.append([Paragraph(skip_text, body_style)])

        # Why it matters conceptually
        why_text = f"<b>Conceptual Takeaway:</b> <i>{why_matters}</i>"
        rows.append([Paragraph(why_text, ParagraphStyle('WhyP', parent=body_style, textColor=colors.HexColor("#2B6CB0")))])

        # Testing & Deliverable
        test_text = f"<b>Validation Test:</b> {test_plan}<br/><b>Deliverable Checkpoint:</b> <b>{deliverable}</b>"
        rows.append([Paragraph(test_text, body_style)])

        card = Table([[top_bar]] + [[Table(rows, colWidths=[PAGE_WIDTH-12])]], colWidths=[PAGE_WIDTH])
        card.setStyle(TableStyle([
            ('BACKGROUND', (0,0), (-1,-1), C_BG_LIGHT),
            ('BOX', (0,0), (-1,-1), 0.5, C_BORDER),
            ('TOPPADDING', (0,0), (-1,-1), 0),
            ('BOTTOMPADDING', (0,1), (-1,1), 4),
            ('LEFTPADDING', (0,1), (-1,1), 5),
            ('RIGHTPADDING', (0,1), (-1,1), 5),
        ]))
        return KeepTogether([card, Spacer(1, 6)])

    story = []

    # =========================================================================
    # PAGE 1: THE 80/20 LEARNING PHILOSOPHY & TOIL VS. INSIGHT MATRIX
    # =========================================================================
    story.append(Spacer(1, 4))
    story.append(Paragraph("THE PRAGMATIC DBMS IMPLEMENTATION ROADMAP", title_style))
    story.append(Paragraph("Maximizing Deep Architectural Mastery While Eliminating Low-Level Toil", subtitle_style))
    
    meta_card = Table([
        [
            Paragraph("<b>Focus:</b> High-Leverage Systems Concepts & Real Architectural Intuition<br/>"
                      "<b>Courses:</b> CMU 15-445 (Core Relational) & CMU 15-721 (Modern OLAP / DuckDB)<br/>"
                      "<b>Workspace:</b> <code>/home/omar/Learning Projects/db-internals</code>", meta_style),
            Paragraph("<b>The 80/20 Rule:</b> Focus 100% on core algorithms, memory hierarchy, and concurrency.<br/>"
                      "<b>Toil Elimination:</b> Strip raw SQL lexers, variable-length defragmentation, and LLVM bytecode.<br/>"
                      "<b>Baseline Status:</b> Disk Manager (Done) • Buffer Pool (Done) • B+ Tree (Done)", meta_style)
        ]
    ], colWidths=[PAGE_WIDTH*0.52, PAGE_WIDTH*0.48])
    meta_card.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,-1), C_BG_LIGHT),
        ('BOX', (0,0), (-1,-1), 0.5, C_BORDER),
        ('PADDING', (0,0), (-1,-1), 5),
        ('VALIGN', (0,0), (-1,-1), 'TOP'),
    ]))
    story.append(meta_card)
    story.append(Spacer(1, 6))

    story.append(make_section_banner("1. The 80/20 Learning Philosophy: Toil vs. Deep Insight", "The Systems Engineering Paradox in Database Learning"))
    story.append(Spacer(1, 5))

    story.append(Paragraph(
        "Building a database from scratch is universally considered the best way to understand systems programming. "
        "However, many engineers fall into the <b>'Low-Level Toil Trap'</b>: spending weeks debugging byte alignment for variable-length "
        "strings or writing recursive-descent SQL parsers. These exercises teach compiler mechanics and pointer math, "
        "<b>not database architecture</b>. By skipping or simplifying low-level toil, you reach the high-leverage concepts—"
        "Volcano execution, Hash Joins, Cost-Based Access Path Selection, MVCC, and Vectorized SIMD processing—in a fraction of the time.",
        body_style
    ))

    # Toil vs Insight Table
    toil_data = [
        [
            Paragraph("Component / Topic", tbl_header),
            Paragraph("What Takes Weeks (Low-ROI Toil)", tbl_header),
            Paragraph("What Gives 90% Conceptual Mastery (High-ROI Core)", tbl_header),
            Paragraph("Pragmatic Decision", tbl_header)
        ],
        [
            Paragraph("<b>Record Storage & Tuples</b>", tbl_cell_bold),
            Paragraph("Writing custom byte serializers for 10 SQL data types, null bitmaps, endianness, and in-page slot compaction.", tbl_cell),
            Paragraph("Fixed-size records (e.g. <code>struct { int64_t id; char name[32]; }</code>) + <code>(Key -> RID)</code> index hookup.", tbl_cell),
            Paragraph("<font color='#C53030'><b>SIMPLIFY</b><br/>Fixed Tuples</font>", tbl_cell_badge_skip)
        ],
        [
            Paragraph("<b>SQL Parsing & Binding</b>", tbl_cell_bold),
            Paragraph("Writing a Lexer, recursive-descent Parser, and AST visitor from scratch (Compilers 101).", tbl_cell),
            Paragraph("Constructing query plans directly in C++ code: <code>HashJoin(SeqScan(), IndexScan())</code>.", tbl_cell),
            Paragraph("<font color='#C53030'><b>SKIP PARSER</b><br/>Programmatic Plan</font>", tbl_cell_badge_skip)
        ],
        [
            Paragraph("<b>Volcano Execution & Joins</b>", tbl_cell_bold),
            Paragraph("Over-engineering abstract type polymorphism and 20 SQL scalar functions.", tbl_cell),
            Paragraph("The <code>Init()</code>/<code>Next()</code> pull pipeline, <b>Hash Join</b> (Build vs Probe), and sequential/index scans.", tbl_cell),
            Paragraph("<font color='#22543D'><b>MUST BUILD</b><br/>Pure Gold</font>", tbl_cell_badge_done)
        ],
        [
            Paragraph("<b>Optimizer & Access Path</b>", tbl_cell_bold),
            Paragraph("Full dynamic programming join enumeration for 10+ table joins.", tbl_cell),
            Paragraph("<b>Access Path Selection</b>: understanding why and when the DB chooses an Index Scan vs a Sequential Scan based on selectivity.", tbl_cell),
            Paragraph("<font color='#22543D'><b>MUST BUILD</b><br/>High Insight</font>", tbl_cell_badge_done)
        ],
        [
            Paragraph("<b>Transactions & MVCC</b>", tbl_cell_bold),
            Paragraph("Writing a complex multi-granularity lock escalation table with strict deadlock graph traversal.", tbl_cell),
            Paragraph("<b>MVCC Snapshot Isolation</b> (<code>xmin</code>, <code>xmax</code>): understanding why readers never block writers and writers never block readers.", tbl_cell),
            Paragraph("<font color='#22543D'><b>MUST BUILD</b><br/>Modern Core</font>", tbl_cell_badge_done)
        ],
        [
            Paragraph("<b>Crash Recovery & ARIES</b>", tbl_cell_bold),
            Paragraph("Implementing multi-pass ARIES Undo with Compensation Log Records (CLRs) across non-quiescent checkpoints.", tbl_cell),
            Paragraph("The <b>WAL Flush Invariant</b> (<code>page_lsn <= flushed_lsn</code>) and Redo-only crash recovery replay.", tbl_cell),
            Paragraph("<font color='#C53030'><b>SIMPLIFY</b><br/>Redo-Only WAL</font>", tbl_cell_badge_skip)
        ],
        [
            Paragraph("<b>Modern OLAP (15-721)</b>", tbl_cell_bold),
            Paragraph("Emitting raw LLVM IR machine code (compilation latency often exceeds execution time).", tbl_cell),
            Paragraph("<b>Vectorized Execution (DuckDB Model)</b>: processing batches of 1024 integers with SIMD filter pushdown.", tbl_cell),
            Paragraph("<font color='#22543D'><b>MUST BUILD</b><br/>50x Speedup</font>", tbl_cell_badge_done)
        ]
    ]

    toil_tbl = Table(toil_data, colWidths=[95, 145, 200, 88])
    toil_tbl.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,0), C_PRIMARY),
        ('GRID', (0,0), (-1,-1), 0.5, C_BORDER),
        ('TOPPADDING', (0,0), (-1,-1), 2.5),
        ('BOTTOMPADDING', (0,0), (-1,-1), 2.5),
        ('LEFTPADDING', (0,0), (-1,-1), 4),
        ('RIGHTPADDING', (0,0), (-1,-1), 4),
        ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
        ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, C_BG_LIGHT]),
    ]))
    story.append(toil_tbl)
    story.append(Spacer(1, 6))

    story.append(make_callout(
        "<b>The Verdict on Your Progress:</b> You have already conquered the hardest, lowest-level foundation of the entire system: "
        "the <b>Buffer Pool Manager with LRU-K</b> and the <b>B+ Tree with Latch Crabbing</b>. "
        "Do not lose momentum in tedious bit-packing. By adopting simplified fixed-size records and programmatic query plans, "
        "you can build a functioning Volcano execution engine with Hash Joins and MVCC in days rather than months!",
        "Strategic Takeaway", "success"
    ))

    # =========================================================================
    # PAGE 2: HIGH-ROI ARCHITECTURAL BLUEPRINT
    # =========================================================================
    story.append(PageBreak())
    story.append(make_section_banner("2. Pragmatic System Architecture Blueprint", "The Lean Database Anatomy"))
    story.append(Spacer(1, 5))

    story.append(Paragraph(
        "Instead of 10 heavily coupled layers with custom compilers and complex recovery managers, "
        "the pragmatic architecture isolates the database engine into <b>6 high-ROI modules</b>. "
        "Notice how the B+ Tree you already built connects directly to the Table Heap and the Execution Engine:",
        body_style
    ))
    story.append(Spacer(1, 4))

    lean_layers = [
        [
            Paragraph("<b>Layer 6: Modern OLAP Engine (CMU 15-721)</b>", tbl_header),
            Paragraph("<b>Vectorized Batch Processing (DuckDB Model)</b>: Operators process <code>VectorBatch[1024]</code> with SIMD filter pushdown (AVX2/AVX-512) and morsel-driven work-stealing parallelism.", tbl_cell)
        ],
        [
            Paragraph("<b>Layer 5: Concurrency & Transactions</b>", tbl_header),
            Paragraph("<b>MVCC Snapshot Isolation</b>: Tuples carry <code>(xmin, xmax)</code>. Transactions read consistent snapshots without acquiring read locks. Write-Ahead Logging (WAL) ensures durability.", tbl_cell)
        ],
        [
            Paragraph("<b>Layer 4: Execution Engine (Volcano)</b>", tbl_header),
            Paragraph("<b>Iterator Pipeline (Init/Next)</b>: <code>SeqScan</code>, <code>IndexScan</code>, <code>HashJoin</code> (Build & Probe), and Group-By Aggregations. Direct programmatic C++ query plans.", tbl_cell)
        ],
        [
            Paragraph("<b>Layer 3: Table Storage & Index Hookup</b>", tbl_header),
            Paragraph("<b>Fixed-Size TableHeap</b>: 4KB pages storing fixed records. Record IDs (<code>RID = page_id : slot</code>) stored as values in your <b>B+ Tree</b>: <code>Index.Insert(Key, RID)</code>.", tbl_cell)
        ],
        [
            Paragraph("<b>Layer 2: Memory & Buffer Pool [DONE]</b>", tbl_header),
            Paragraph("<b>4KB Page Frames</b>: LRU-K (K=2) eviction, zero-copy pointer casting over <code>page->GetData()</code>, pin/unpin tracking.", tbl_cell_bold)
        ],
        [
            Paragraph("<b>Layer 1: Storage & OS Interface [DONE]</b>", tbl_header),
            Paragraph("<b>POSIX Block I/O + io_uring</b>: Synchronous <code>pread/pwrite</code> and Linux <code>io_uring</code> asynchronous batched queues.", tbl_cell_bold)
        ]
    ]

    lean_tbl = Table(lean_layers, colWidths=[165, PAGE_WIDTH - 165])
    lean_tbl.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (0,-1), C_SECONDARY),
        ('GRID', (0,0), (-1,-1), 0.5, C_BORDER),
        ('TOPPADDING', (0,0), (-1,-1), 4),
        ('BOTTOMPADDING', (0,0), (-1,-1), 4),
        ('LEFTPADDING', (0,0), (-1,-1), 6),
        ('RIGHTPADDING', (0,0), (-1,-1), 6),
        ('ROWBACKGROUNDS', (1,0), (1,1), [C_WARNING_BG, colors.white]),
        ('ROWBACKGROUNDS', (1,2), (1,3), [C_INFO_BG, colors.white]),
        ('ROWBACKGROUNDS', (1,4), (1,5), [C_SUCCESS_BG, colors.white]),
    ]))
    story.append(lean_tbl)
    story.append(Spacer(1, 8))

    story.append(Paragraph("<b>The Golden Invariant: How the B+ Tree Connects to Table Records</b>", h3_style))
    story.append(Paragraph(
        "A common point of confusion when learning databases is: <i>'What does the B+ Tree actually store?'</i><br/>"
        "In your single-page and multi-level tests, your B+ Tree stored <code>int64_t Key -> int64_t Value</code>. "
        "In a relational engine, the <code>Value</code> is simply a <b>Record ID (RID)</b>: <code>struct RID { page_id_t page_id; uint32_t slot; }</code>.<br/>"
        "1. When inserting a row: <code>TableHeap::Insert(row, &rid)</code> writes the row to a table page and returns its <code>RID</code>.<br/>"
        "2. Then: <code>Index::Insert(row.id, rid)</code> inserts the key and its physical record pointer into the B+ Tree.<br/>"
        "3. When querying <code>WHERE id = 42</code>: the execution engine searches the B+ Tree to find <code>RID</code>, then calls <code>TableHeap::GetTuple(rid)</code>. "
        "That is the entire secret of secondary database indexing!",
        body_style
    ))
    story.append(Spacer(1, 6))

    story.append(make_callout(
        "<b>The 'Zero-Toil' Table Layout:</b> Avoid writing variable-length slotted page defragmenters. "
        "Define a fixed row struct: <code>struct Record { int64_t id; char name[32]; double balance; int64_t xmin; int64_t xmax; };</code>. "
        "A 4KB page holds approximately (4096 - header) / sizeof(Record) records. "
        "This unlocks 100% of relational query execution, joins, and MVCC with zero pointer math bugs!",
        "Engineering Shortcut", "warning"
    ))

    # =========================================================================
    # PAGE 3: PHASE 1 - SPRINTS 1.1 & 1.2
    # =========================================================================
    story.append(PageBreak())
    story.append(make_section_banner("3. Phase 1: Pragmatic Core Relational Engine (CMU 15-445)", "Sprints 1.1 through 1.4: Storage Hookup, Volcano Execution & Joins"))
    story.append(Spacer(1, 5))

    story.append(make_sprint_card(
        "1.1", "Storage Consolidation & RAII Page Guards",
        "CMU 15-445 Lecture #03 & #04",
        "Harden your BufferPoolManager and B+ Tree with clean RAII memory safety before adding higher layers.",
        [
            "Implement <code>PageGuard</code>, <code>ReadPageGuard</code>, and <code>WritePageGuard</code> RAII wrappers around <code>Page*</code>.",
            "Automatically invoke <code>bpm->UnpinPage(page_id, is_dirty)</code> and <code>page->RUnlatch() / WUnlatch()</code> in the destructor.",
            "Eliminates 100% of buffer pool frame leaks and dangling latches during exception or early return paths."
        ],
        [
            "Do not write custom memory allocators or specialized disk defragmenters. Rely on your existing POSIX/io_uring disk manager."
        ],
        "Manual unpinning is the #1 source of database deadlocks and out-of-memory crashes. RAII guarantees leak-free execution.",
        "Write <code>PageGuardTest</code>: scope-exit assertions verifying pin count drops back to zero.",
        "Bulletproof RAII memory guard wrapping BufferPoolManager frames.",
        "COMPLETED / HARDENING"
    ))

    story.append(make_sprint_card(
        "1.2", "Fixed-Size TableHeap & B+ Tree Tuple Hookup",
        "CMU 15-445 Lecture #03 (Database Storage: Files, Pages, Tuples)",
        "Connect your existing B+ Tree to a real persistent table storage heap without variable-length byte-packing toil.",
        [
            "Define fixed-size record schema: <code>struct UserRow { int64_t id; char name[32]; double score; int64_t xmin; int64_t xmax; };</code>.",
            "<code>TablePage</code>: 4KB page frame managing an array of fixed records + a 64-bit occupancy bitmap (1 = allocated, 0 = free slot).",
            "<code>RID</code>: <code>uint64_t</code> bit-packed into <code>(page_id: 32 bits, slot_num: 32 bits)</code>.",
            "<code>TableHeap</code>: Doubly linked list of <code>TablePage</code>s (<code>first_page_id</code>, <code>next_page_id</code>). Implements <code>Insert(row, &rid)</code>, <code>Get(rid, &row)</code>, <code>Update(rid, row)</code>, and <code>MarkDelete(rid)</code>.",
            "<b>Index Hookup</b>: In your test harness, instantiate <code>BPlusTree<int64_t, RID></code>. When inserting a row, insert <code>(row.id, rid)</code> into the tree!"
        ],
        [
            "Skip polymorphic <code>Type</code> and <code>Value</code> classes (INTEGER, VARCHAR, etc.). Use native C++ types (`int64_t`, `double`, `char[]`).",
            "Skip slotted-page compaction and defragmentation (fixed slots eliminate internal fragmentation entirely)."
        ],
        "This turns your standalone key-value B+ Tree into a true database index that routes queries to physical disk records.",
        "<code>TableHeapTest</code>: Insert 10,000 rows into <code>TableHeap</code>, index on <code>id</code> with B+ Tree, query random IDs via B+ Tree, fetch row from <code>TableHeap</code>, and assert 100% match.",
        "Fully integrated Table Storage Heap connected to concurrent B+ Tree index.",
        "NEXT UP (CORE PRIORITY)"
    ))

    # =========================================================================
    # PAGE 4: PHASE 1 - SPRINTS 1.3 & 1.4
    # =========================================================================
    story.append(PageBreak())

    story.append(make_sprint_card(
        "1.3", "The Volcano Execution Engine & Relational Joins",
        "CMU 15-445 Lecture #13 (Query Execution) & Lecture #12 (Join Algorithms)",
        "Master the iterator execution model and build the most important analytical algorithm in history: the Hash Join.",
        [
            "<code>AbstractExecutor</code>: Pure virtual interface defining <code>Init()</code>, <code>Next(UserRow* out_row, RID* out_rid) -> bool</code>.",
            "<code>SeqScanExecutor</code>: Sequential scan traversing <code>TableHeap</code> pages and yielding live (non-deleted) rows.",
            "<code>IndexScanExecutor</code>: Traverses B+ Tree index using point search or range iterator, fetches matching RIDs, and loads tuples from <code>TableHeap</code> in O(log N) time.",
            "<code>FilterExecutor</code>: Evaluates a predicate lambda (e.g. <code>[](const UserRow& r) { return r.score > 80.0; }</code>) over child executor.",
            "<b>HashJoinExecutor (Equi-Join)</b>:<br/>"
            "  • <i>Build Phase</i>: In <code>Init()</code>, drains the inner child executor into an in-memory hash table (<code>std::unordered_multimap<int64_t, UserRow></code>).<br/>"
            "  • <i>Probe Phase</i>: In <code>Next()</code>, pulls rows from the outer child, probes the hash table, and yields matched combinations."
        ],
        [
            "Skip writing a dynamic expression evaluator or AST parser. Use C++ lambdas for filter conditions and join keys.",
            "Skip 3-way external merge sort and disk-spilling joins for now (focus on in-memory hash tables)."
        ],
        "Volcano execution powers SQLite, Postgres, and MySQL. The Hash Join eliminates O(N &times; M) quadratic explosions and is the workhorse of relational databases.",
        "<code>HashJoinTest</code>: Join 5,000 customers with 20,000 transactions; verify Hash Join produces exact matching rows in milliseconds compared to seconds for Nested Loop Join.",
        "Working Volcano iterator engine executing scans, filters, and high-speed hash joins.",
        "CORE PRIORITY"
    ))

    story.append(make_sprint_card(
        "1.4", "Cost-Based Optimizer & Dynamic Programming (DP) Join Ordering",
        "CMU 15-445 Lecture #15 & #16 (Query Optimization Part 1 & 2)",
        "Master cost modeling, single-table access path selection, and multi-table dynamic programming join ordering (Selinger algorithm).",
        [
            "Table Statistics Tracker: Maintain row count N, page count P, and distinct key count V(A) for indexed columns.",
            "Cost Modeling Formulas:<br/>"
            "  • <b>SeqScan Cost</b>: C_seq = P &times; Cost_page_seq + N &times; Cost_cpu_tuple.<br/>"
            "  • <b>IndexScan Cost</b>: C_idx = (h + ceil(sel &times; P_leaf)) &times; Cost_page_seq + (sel &times; N) &times; Cost_page_random + (sel &times; N) &times; Cost_cpu_tuple.<br/>"
            "  • <b>Join Cost Models</b>: NestedLoopJoin (O(|Outer| &times; |Inner|)) vs HashJoin (O(|Outer| + |Inner|)).",
            "Access Path Selector: Compare C_seq vs C_idx based on selectivity sel (crossover at ~25-30%).",
            "Rule-Based Filter Pushdown: Push filter predicates below joins directly into base scan operators.",
            "<b>Selinger Dynamic Programming (DP) Join Ordering</b>: Enumerate all (2^N - 1) relation subsets using bitmasks (1 &lt;&lt; i). Memoize the optimal join tree and physical algorithm (NLJ vs HashJoin) for every subset."
        ],
        [
            "Skip complex hyper-graph partitioning for 50+ tables (greedy heuristics). Bitmask DP for up to 8-10 tables is optimal and standard.",
            "Skip complex histogram bucketing (use uniform distribution assumption: sel = 1 / V(A))."
        ],
        "The query optimizer is the 'brain' of a relational database. It prevents catastrophic factorial O(N!) join order blunders and random disk I/O thrashing.",
        "<code>OptimizerDPTest</code>: Selectivity crossover (0.1% picks IndexScan, 40% picks SeqScan), filter pushdown, and 3-way/4-way DP join order selection.",
        "Cost-based query planner selecting optimal access paths and DP join orders based on I/O cost estimation.",
        "CORE PRIORITY"
    ))

    # =========================================================================
    # PAGE 5: PHASE 1 - SPRINTS 1.5 & 1.6
    # =========================================================================
    story.append(PageBreak())

    story.append(make_sprint_card(
        "1.5", "Concurrency Control: MVCC Snapshot Isolation",
        "CMU 15-445 Lecture #17, #18, #20 (Concurrency Control & MVCC)",
        "Implement Multi-Version Concurrency Control so readers never block writers and writers never block readers.",
        [
            "Tuple Version Header: Augment `UserRow` with <code>int64_t xmin</code> (creating transaction ID) and <code>int64_t xmax</code> (deleting transaction ID, 0 if alive).",
            "Transaction Manager: Assigns monotonically increasing `txn_id`. Maintains <code>active_txn_set</code>.",
            "Snapshot Creation: When transaction T begins, capture <code>Snapshot = {T.id, active_txns}</code>.",
            "Visibility Rule: A row is visible to transaction T if:<br/>"
            "  1. <code>row.xmin &lt;= T.id</code> AND <code>row.xmin</code> was committed when T started.<br/>"
            "  2. <code>row.xmax == 0</code> OR <code>row.xmax &gt; T.id</code> OR <code>row.xmax</code> belongs to an active uncommitted transaction.",
            "Non-Blocking Reads: <code>SeqScan</code> and <code>IndexScan</code> check visibility without taking shared read locks!"
        ],
        [
            "Skip multi-granularity hierarchical lock escalation (IS/IX/S/X tables). MVCC provides superior concurrency with simpler mechanics.",
            "Skip out-of-line rollback segments / undo log pointer chains. Use in-place append-only version records for initial validation."
        ],
        "MVCC is the foundation of PostgreSQL, Oracle, and MySQL InnoDB. Understanding snapshot visibility eliminates 99% of concurrency bugs.",
        "<code>MVCCConcurrencyTest</code>: Spawn 1 writer thread continuously updating rows and 5 reader threads continuously calculating `SUM(score)`; assert readers see consistent snapshots with zero lock pauses.",
        "Non-blocking MVCC query execution engine with snapshot isolation.",
        "CORE PRIORITY"
    ))

    story.append(make_sprint_card(
        "1.6", "Write-Ahead Logging (WAL) & Redo Crash Replay",
        "CMU 15-445 Lecture #21 & #22 (Write-Ahead Logging & Crash Recovery)",
        "Guarantee ACID Durability using the core WAL flush invariant without drowning in complex ARIES undo CLRs.",
        [
            "<code>LogRecord</code>: Binary struct <code>{lsn_t lsn, txn_id_t txn_id, LogType type, RID rid, UserRow before_image, UserRow after_image}</code>.",
            "<code>LogManager</code>: In-memory append-only ring buffer + background flusher thread flushes log buffer to disk.",
            "<b>The Core WAL Invariant</b>: In <code>BufferPoolManager::FlushPage(page_id)</code>, enforce:<br/>"
            "  <code>page->GetLSN() <= log_manager->GetFlushedLSN()</code>.<br/>"
            "  If the page has an unflushed LSN, the log buffer must be flushed to disk before writing the 4KB data frame!",
            "Group Commit: Batch multiple transaction commit flushes into a single synchronous disk write.",
            "Crash Recovery (Redo-Only): On database restart, scan the WAL file forward from start to end; for any committed transaction whose <code>record.lsn > page->GetLSN()</code>, re-apply the <code>after_image</code> to the page."
        ],
        [
            "Skip the full ARIES 3-pass Undo phase with Compensation Log Records (CLRs). Undo logic across arbitrary crashes is notoriously brittle.",
            "Skip non-quiescent fuzzy checkpointing during initial implementation (use simple clean sync checkpoints)."
        ],
        "The WAL invariant is the single most important rule in database durability: log records must precede dirty pages to disk.",
        "<code>CrashRecoveryTest</code>: Insert 5,000 rows, commit, kill process with <code>kill -9</code> before dirty buffer pages are flushed to disk; restart engine, invoke Redo recovery, and assert all 5,000 rows exist.",
        "Crash-resilient database storage engine satisfying ACID Durability.",
        "CORE PRIORITY"
    ))

    # =========================================================================
    # PAGE 6: PHASE 1 CAPSTONE & PHASE 2 INTRO
    # =========================================================================
    story.append(PageBreak())

    story.append(make_sprint_card(
        "1.7", "Interactive SQL Serving (Postgres Wire Protocol or CLI)",
        "CMU 15-445 Lecture #14 & CMU 15-721 Lecture #12 (Database Networking)",
        "Turn your storage and execution engine into an interactive server accessible via standard terminal tools.",
        [
            "Upgrade <code>threaded_server.c</code> to parse basic PostgreSQL Wire Protocol v3.0 messages:",
            "  • Handshake: Respond to <code>StartupMessage</code> with <code>AuthenticationOk ('R')</code> and <code>ReadyForQuery ('Z')</code>.",
            "  • Query: Receive <code>'Q'</code> (raw query string), dispatch to parser/executor, return <code>RowDescription ('T')</code>, <code>DataRow ('D')</code>, and <code>CommandComplete ('C')</code>.",
            "Alternative (Even Leaner): Build a clean interactive REPL terminal CLI (like SQLite's command prompt) that parses simple commands: <code>INSERT</code>, <code>SELECT</code>, <code>JOIN</code>, and <code>EXPLAIN</code>."
        ],
        [
            "Do not implement SSL/TLS encryption, complex scram-sha-256 authentication, or extended query protocol (Parse/Bind/Execute phases)."
        ],
        "Connecting real clients (like `psql` or Python scripts) proves that your database is a real system, not just an academic library.",
        "<code>PsqlConnectionTest</code>: Run <code>psql -h 127.0.0.1 -p 5432</code> and execute interactive queries successfully.",
        "Interactive database server reachable by standard database client tools.",
        "PHASE 1 CAPSTONE"
    ))

    story.append(Spacer(1, 6))
    story.append(make_section_banner("4. Phase 2: Modern OLAP & Vectorized Execution (CMU 15-721)", "The Analytical Revolution: SIMD, Columnar Storage & DuckDB Architecture"))
    story.append(Spacer(1, 5))

    story.append(Paragraph(
        "In modern data processing, disk I/O is no longer the sole bottleneck. With NVMe SSDs streaming at 7 GB/s, "
        "the CPU and memory bus become the primary choke points. CMU 15-721 teaches how modern engines (DuckDB, ClickHouse, Snowflake) "
        "process <b>billions of rows per second</b> by replacing tuple-at-a-time Volcano execution with <b>columnar vector batches</b>.",
        body_style
    ))
    story.append(Spacer(1, 4))

    story.append(make_sprint_card(
        "2.1", "Columnar Storage & Dictionary Encoding",
        "CMU 15-721 Lecture #01, #02, #03 (Modern OLAP & Data Formats)",
        "Store data column-by-column rather than row-by-row to maximize CPU cache utilization during analytical aggregations.",
        [
            "Columnar Chunk Representation: Store each column as a contiguous array: <code>int64_t ids[]</code>, <code>double scores[]</code>.",
            "Dictionary Encoding: Replace repetitive strings with 1-byte or 2-byte integer codes mapped to a unique string dictionary.",
            "Run-Length Encoding (RLE): Compress sorted or repetitive columns into <code>(value, run_length)</code> pairs.",
            "Analytical Scan Benchmark: Compute <code>SUM(score)</code> by scanning only the <code>score</code> column, skipping all other attributes entirely!"
        ],
        [
            "Skip full Apache Parquet format decoders (Parquet involves complex Thrift metadata and nested Dremel repetition levels).",
            "Implement in-memory columnar chunks (Arrow RecordBatch style)."
        ],
        "Columnar storage allows analytical queries to read only the columns they need, reducing memory bus traffic by 80–90%.",
        "<code>ColumnarScanBenchmark</code>: Compare calculating <code>SUM(score)</code> over 10 million rows in Row format vs Columnar format; measure memory bandwidth and cache misses.",
        "High-performance columnar storage layout with dictionary compression.",
        "PHASE 2 (HIGH ROI)"
    ))

    # =========================================================================
    # PAGE 7: PHASE 2 - SPRINTS 2.2 & 2.3
    # =========================================================================
    story.append(PageBreak())

    story.append(make_sprint_card(
        "2.2", "Vectorized Batch Execution & SIMD Processing (The DuckDB Model)",
        "CMU 15-721 Lecture #04, #05, #06 (Vectorized Execution Using SIMD)",
        "Replace Volcano tuple-at-a-time execution with 1024-element vector batch processing to achieve 50x-100x speedup.",
        [
            "The Vectorized Iterator: Instead of <code>bool Next(Tuple* out)</code>, operators implement:<br/>"
            "  <code>int Next(VectorBatch* batch)</code> where <code>VectorBatch</code> holds arrays of 1024 values per column.",
            "Selection Vectors: Represent filter results as an array of active row offsets (e.g. <code>uint16_t sel_vector[]</code>), completely eliminating data copying during filter passes.",
            "SIMD Vector Primitives (AVX2):<br/>"
            "  • Use AVX2 intrinsics (<code>_mm256_cmpgt_epi32</code>) to evaluate filter conditions (e.g. <code>WHERE age > 30</code>) at 8 integers per CPU cycle.<br/>"
            "  • Use horizontal SIMD additions (<code>_mm256_add_epi32</code>) for aggregations (`SUM`, `AVG`)."
        ],
        [
            "Skip LLVM IR machine code generation (JIT compilation). Vectorized execution delivers ~95% of compiled performance with 1/10th the complexity.",
            "Skip dynamic JIT template engines (DuckDB proved vectorized interpretation beats JIT for interactive queries)."
        ],
        "This is the core design pattern that made DuckDB and ClickHouse the fastest embedded analytical engines in the world.",
        "<code>VectorizedVsVolcanoBenchmark</code>: Run <code>WHERE score > 50.0</code> over 20,000,000 records; observe Volcano takes ~600ms while Vectorized execution takes ~8ms (a 75x speedup!).",
        "Vectorized execution engine processing hundreds of millions of values per second per core.",
        "PHASE 2 (CROWN JEWEL)"
    ))

    story.append(make_sprint_card(
        "2.3", "Morsel-Driven Work-Stealing Parallelism",
        "CMU 15-721 Lecture #08 (Query Scheduling & Coordination)",
        "Scale query execution linearly across all available CPU cores using dynamic morsel-driven scheduling.",
        [
            "Morsel Dispatcher: Partition input table scans into dynamic, fixed-size chunks ('morsels' of 10,000 to 50,000 rows).",
            "Lock-Free Work-Stealing Thread Pool: Worker threads dynamically pull morsels from queues, stealing from neighbor queues when idle to eliminate straggler cores.",
            "Thread-Local Aggregations: Each thread accumulates partial aggregate hash tables, followed by a fast parallel reduction merge.",
            "Multicore Scaling Test: Measure speedup scaling from 1 core to 2, 4, 8, and 16 CPU cores."
        ],
        [
            "Skip complex hardware NUMA node binding configurations (focus on dynamic work-stealing thread pools first).",
            "Skip distributed shuffle networks (keep execution single-node multicore)."
        ],
        "Static query partitioning causes severe imbalances when data is skewed. Morsel-driven execution guarantees 100% CPU core utilization.",
        "<code>ParallelScalingBenchmark</code>: Run analytical query on 50 million rows across 1, 2, 4, and 8 threads; verify near-linear scaling curve.",
        "Elastic multicore parallel query scheduler with work-stealing load balancing.",
        "PHASE 2 (HIGH ROI)"
    ))

    # =========================================================================
    # PAGE 8: STREAMLINED LECTURE SYNCHRONIZATION MATRIX
    # =========================================================================
    story.append(PageBreak())
    story.append(make_section_banner("5. Streamlined Lecture Synchronization Matrix", "CMU 15-445 & 15-721 Action Guide: What to Watch, What to Build, What to Skip"))
    story.append(Spacer(1, 5))

    story.append(Paragraph(
        "Use this matrix as your tactical study roadmap. Notice the clear distinction between lectures that require code "
        "versus lectures that are purely conceptual or can be skimmed:",
        body_style
    ))
    story.append(Spacer(1, 4))

    matrix_data = [
        [
            Paragraph("Course & Lecture", tbl_header),
            Paragraph("Lecture Title", tbl_header),
            Paragraph("Key Architectural Concept", tbl_header),
            Paragraph("Action: Build, Simplify, or Skip", tbl_header)
        ],
        # INTRO 15-445
        [Paragraph("15-445 #01-02", tbl_cell_bold), Paragraph("Relational Model & Modern SQL", tbl_cell), Paragraph("Relational algebra, set operations.", tbl_cell), Paragraph("<font color='#718096'>SKIM (Theory only)</font>", tbl_cell_badge_skip)],
        [Paragraph("15-445 #03", tbl_cell_bold), Paragraph("Database Storage: Files, Pages, Tuples", tbl_cell), Paragraph("Slotted page layout, Record IDs (RID).", tbl_cell), Paragraph("<font color='#22543D'><b>BUILD (Sprint 1.2: Fixed TableHeap)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-445 #04", tbl_cell_bold), Paragraph("Memory Management & Buffer Pools", tbl_cell), Paragraph("Frame cache, LRU-K, pin/unpin.", tbl_cell), Paragraph("<font color='#22543D'><b>COMPLETED (Your BPM is done!)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-445 #05", tbl_cell_bold), Paragraph("Log-Structured Storage (SingleStore)", tbl_cell), Paragraph("LSM-Trees, MemTable, SSTables.", tbl_cell), Paragraph("<font color='#718096'>WATCH (Skip coding for now)</font>", tbl_cell_badge_skip)],
        [Paragraph("15-445 #06", tbl_cell_bold), Paragraph("Column-Store Databases", tbl_cell), Paragraph("PAX format, analytical decomposition.", tbl_cell), Paragraph("<font color='#2B6CB0'>WATCH (Prepares for Phase 2)</font>", tbl_cell_badge_next)],
        [Paragraph("15-445 #07", tbl_cell_bold), Paragraph("Database Hash Tables (YugabyteDB)", tbl_cell), Paragraph("Hash tables for joins and aggs.", tbl_cell), Paragraph("<font color='#22543D'><b>BUILD (Sprint 1.3: Hash Join)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-445 #08", tbl_cell_bold), Paragraph("B+Trees: The Best Data Structure", tbl_cell), Paragraph("Node splits, merges, tree depth.", tbl_cell), Paragraph("<font color='#22543D'><b>COMPLETED (Your B+ Tree is done!)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-445 #09", tbl_cell_bold), Paragraph("Vector, Inverted Indexes & Filters", tbl_cell), Paragraph("Bloom filters, HNSW vector search.", tbl_cell), Paragraph("<font color='#718096'>SKIM (Optional for now)</font>", tbl_cell_badge_skip)],
        [Paragraph("15-445 #10", tbl_cell_bold), Paragraph("Latching in Data Structures", tbl_cell), Paragraph("Read/Write latch crabbing, coupling.", tbl_cell), Paragraph("<font color='#22543D'><b>COMPLETED (Milestone 7-8 done!)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-445 #11", tbl_cell_bold), Paragraph("Sorting & Aggregation Algorithms", tbl_cell), Paragraph("External merge sort, hashing aggs.", tbl_cell), Paragraph("<font color='#2B6CB0'>SIMPLIFY (In-memory sort & aggs)</font>", tbl_cell_badge_next)],
        [Paragraph("15-445 #12", tbl_cell_bold), Paragraph("Joins: Hash, Sort-Merge, Nested Loop", tbl_cell), Paragraph("Hash join build/probe algorithms.", tbl_cell), Paragraph("<font color='#22543D'><b>BUILD (Sprint 1.3: Hash Join)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-445 #13", tbl_cell_bold), Paragraph("Query Execution Part 1", tbl_cell), Paragraph("Volcano iterator model (Init/Next).", tbl_cell), Paragraph("<font color='#22543D'><b>BUILD (Sprint 1.3: Volcano Engine)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-445 #14", tbl_cell_bold), Paragraph("Parallel Query Engine Architectures", tbl_cell), Paragraph("Process models, thread pools.", tbl_cell), Paragraph("<font color='#2B6CB0'>WATCH (Prepares for Server / Morsels)</font>", tbl_cell_badge_next)],
        [Paragraph("15-445 #15-16", tbl_cell_bold), Paragraph("Query Optimization Part 1 & 2", tbl_cell), Paragraph("Cost models, Access path selection.", tbl_cell), Paragraph("<font color='#22543D'><b>BUILD (Sprint 1.4: Access Path Cost)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-445 #17-19", tbl_cell_bold), Paragraph("Concurrency Theory & 2-Phase Locking", tbl_cell), Paragraph("Serializability, 2PL, deadlocks.", tbl_cell), Paragraph("<font color='#2B6CB0'>WATCH (Study theory, jump to MVCC)</font>", tbl_cell_badge_next)],
        [Paragraph("15-445 #20", tbl_cell_bold), Paragraph("MVCC: Multi-Version Concurrency", tbl_cell), Paragraph("Snapshot isolation, xmin/xmax headers.", tbl_cell), Paragraph("<font color='#22543D'><b>BUILD (Sprint 1.5: Snapshot Reads)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-445 #21-22", tbl_cell_bold), Paragraph("WAL Logging & Crash Recovery (ARIES)", tbl_cell), Paragraph("WAL invariant, Analysis/Redo passes.", tbl_cell), Paragraph("<font color='#22543D'><b>BUILD (Sprint 1.6: Redo-Only WAL)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-445 #23-25", tbl_cell_bold), Paragraph("Distributed Databases & Cloud Warehouses", tbl_cell), Paragraph("2PC, consensus, distributed queries.", tbl_cell), Paragraph("<font color='#718096'>WATCH (High-level systems review)</font>", tbl_cell_badge_skip)],
        
        # ADVANCED 15-721
        [Paragraph("15-721 #01", tbl_cell_bold), Paragraph("Modern OLAP Database Systems", tbl_cell), Paragraph("Architecture of analytical engines.", tbl_cell), Paragraph("<font color='#2B6CB0'>WATCH (High priority for Phase 2)</font>", tbl_cell_badge_next)],
        [Paragraph("15-721 #02-03", tbl_cell_bold), Paragraph("Data Formats & Encoding Part 1 & 2", tbl_cell), Paragraph("Dictionary, Bitpacking, FSST.", tbl_cell), Paragraph("<font color='#22543D'><b>BUILD (Sprint 2.1: Columnar Chunks)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-721 #04-05", tbl_cell_bold), Paragraph("Query Execution & Processing", tbl_cell), Paragraph("Vectorized vs Push compilation.", tbl_cell), Paragraph("<font color='#22543D'><b>BUILD (Sprint 2.2: Vector Batch)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-721 #06", tbl_cell_bold), Paragraph("Vectorized Execution Using SIMD", tbl_cell), Paragraph("AVX2/AVX-512 filter bitmasks.", tbl_cell), Paragraph("<font color='#22543D'><b>BUILD (Sprint 2.2: SIMD Filters)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-721 #07", tbl_cell_bold), Paragraph("JIT Compilation & Code Generation", tbl_cell), Paragraph("HyPer code gen vs DuckDB vectorization.", tbl_cell), Paragraph("<font color='#C53030'>WATCH ONLY (Skip raw LLVM IR coding)</font>", tbl_cell_badge_skip)],
        [Paragraph("15-721 #08", tbl_cell_bold), Paragraph("Query Scheduling & Coordination", tbl_cell), Paragraph("Morsel-driven work-stealing parallelism.", tbl_cell), Paragraph("<font color='#22543D'><b>BUILD (Sprint 2.3: Morsel Scheduler)</b></font>", tbl_cell_badge_done)],
        [Paragraph("15-721 #09-10", tbl_cell_bold), Paragraph("Parallel Hash Joins & WCOJ", tbl_cell), Paragraph("Radix hash join, Leapfrog Triejoin.", tbl_cell), Paragraph("<font color='#718096'>SKIM (Niche graph theory; skip WCOJ)</font>", tbl_cell_badge_skip)],
        [Paragraph("15-721 #12-15", tbl_cell_bold), Paragraph("Networking & Cascades Optimizer", tbl_cell), Paragraph("Arrow Flight, Memo optimizer search.", tbl_cell), Paragraph("<font color='#2B6CB0'>WATCH (Concept study)</font>", tbl_cell_badge_next)],
        [Paragraph("15-721 #17-22", tbl_cell_bold), Paragraph("DuckDB, Photon, Snowflake, BigQuery", tbl_cell), Paragraph("Case studies of commercial cloud engines.", tbl_cell), Paragraph("<font color='#718096'>WATCH (Excellent case studies)</font>", tbl_cell_badge_skip)],
    ]

    matrix_tbl = Table(matrix_data, colWidths=[68, 140, 160, 160])
    matrix_tbl.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,0), C_PRIMARY),
        ('GRID', (0,0), (-1,-1), 0.5, C_BORDER),
        ('TOPPADDING', (0,0), (-1,-1), 2),
        ('BOTTOMPADDING', (0,0), (-1,-1), 2),
        ('LEFTPADDING', (0,0), (-1,-1), 3),
        ('RIGHTPADDING', (0,0), (-1,-1), 3),
        ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
        ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, C_BG_LIGHT]),
    ]))
    story.append(matrix_tbl)

    # =========================================================================
    # PAGE 9: ENGINEERING RIGOR & IMMEDIATE ACTION ITEM
    # =========================================================================
    story.append(PageBreak())
    story.append(make_section_banner("6. Toolchain, Benchmarking & Immediate Action Plan", "Verification Standards and the Next 7 Days"))
    story.append(Spacer(1, 5))

    story.append(Paragraph(
        "To ensure that your implementation is industrial-grade and crash-resilient, "
        "apply these exact compiler sanitizers and benchmarking tools throughout your development:",
        body_style
    ))
    story.append(Spacer(1, 4))

    rigor_items = [
        "<b>AddressSanitizer (ASan) & UndefinedBehaviorSanitizer (UBSan):</b> Always compile with <code>-fsanitize=address,undefined -g -O1</code>. Catches buffer overflows, out-of-bounds reads in table pages, and dangling pointer dereferences.",
        "<b>ThreadSanitizer (TSan):</b> Compile multi-threaded MVCC reader/writer and morsel scheduler tests with <code>-fsanitize=thread -g -O1</code>. Guarantees zero data races on shared page memory.",
        "<b>Microbenchmarking with Perf Counters:</b> Use <code>perf stat -e instructions,cycles,L1-dcache-load-misses,branch-misses</code>. When you compare Volcano vs Vectorized execution in Sprint 2.2, you will directly see IPC jump from ~0.8 to >2.5!"
    ]
    for r in rigor_items:
        story.append(Paragraph(f"• {r}", bullet_style))

    story.append(Spacer(1, 6))

    # Benchmark Guide Table
    story.append(Paragraph("<b>Target Benchmarks for Your Engine:</b>", h3_style))
    bench_data = [
        [
            Paragraph("Benchmark", tbl_header),
            Paragraph("Query Shape", tbl_header),
            Paragraph("Primary Metric", tbl_header),
            Paragraph("What It Proves", tbl_header)
        ],
        [
            Paragraph("<b>Point Index Scan</b>", tbl_cell_bold),
            Paragraph("<code>WHERE id = 42</code>", tbl_cell),
            Paragraph("Latency (&mu;s)", tbl_cell),
            Paragraph("Validates B+ Tree O(log N) traversal fetching matching RID from <code>TableHeap</code> in &lt;5 microseconds.", tbl_cell)
        ],
        [
            Paragraph("<b>Hash Join</b>", tbl_cell_bold),
            Paragraph("<code>Users JOIN Orders ON u.id = o.user_id</code>", tbl_cell),
            Paragraph("Throughput (rows/sec)", tbl_cell),
            Paragraph("Validates that in-memory hash table build/probe outperforms nested loops by 100x+.", tbl_cell)
        ],
        [
            Paragraph("<b>MVCC Snapshot Read</b>", tbl_cell_bold),
            Paragraph("Concurrent table scan while 10 threads update rows.", tbl_cell),
            Paragraph("Zero lock stalls", tbl_cell),
            Paragraph("Proves that readers never block writers and writers never block readers.", tbl_cell)
        ],
        [
            Paragraph("<b>Vectorized Scan (OLAP)</b>", tbl_cell_bold),
            Paragraph("<code>SELECT SUM(score) WHERE score > 50</code>", tbl_cell),
            Paragraph("GB/s scan bandwidth", tbl_cell),
            Paragraph("Demonstrates the 50x speedup of 1024-batch SIMD execution over Volcano.", tbl_cell)
        ]
    ]
    bench_tbl = Table(bench_data, colWidths=[90, 130, 95, 213])
    bench_tbl.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,0), C_PRIMARY),
        ('GRID', (0,0), (-1,-1), 0.5, C_BORDER),
        ('TOPPADDING', (0,0), (-1,-1), 3),
        ('BOTTOMPADDING', (0,0), (-1,-1), 3),
        ('LEFTPADDING', (0,0), (-1,-1), 4),
        ('RIGHTPADDING', (0,0), (-1,-1), 4),
        ('VALIGN', (0,0), (-1,-1), 'TOP'),
        ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, C_BG_LIGHT]),
    ]))
    story.append(bench_tbl)
    story.append(Spacer(1, 8))

    # Action Plan Callout
    story.append(make_callout(
        "<b>Your Immediate Next Step (Next 48 Hours):</b><br/>"
        "1. Open CMU 15-445 <b>Lecture #03 (Database Storage: Files, Pages, Tuples)</b>.<br/>"
        "2. Create <code>src/storage/table/table_page.h</code> and <code>src/storage/table/table_heap.h</code>.<br/>"
        "3. Define a fixed struct: <code>struct UserRow { int64_t id; char name[32]; double score; int64_t xmin; int64_t xmax; };</code>.<br/>"
        "4. Implement <code>TableHeap::Insert(row, &rid)</code> and <code>TableHeap::Get(rid, &row)</code> over your existing <code>BufferPoolManager</code>.<br/>"
        "5. Connect your B+ Tree: insert <code>(row.id, rid)</code> into the tree, search for <code>row.id</code>, fetch the row from <code>TableHeap</code>, and verify it matches!<br/>"
        "<b>You will have a working indexed table storage engine in a single coding session.</b>",
        "Immediate Action Plan: Sprint 1.2", "success"
    ))

    # Build PDF
    doc.build(story, canvasmaker=NumberedCanvas)
    print(f"Pragmatic Roadmap PDF generated successfully: {filename}")

if __name__ == "__main__":
    out_pdf = "/home/omar/Learning Projects/db-internals/DBMS_Implementation_Roadmap.pdf"
    build_pdf(out_pdf)
