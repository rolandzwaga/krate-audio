<?xml version="1.0" encoding="UTF-8"?>
<!--
  t222-sweep-controls.xsl - fix plan T2.22 (D-069): the missing sweep controls and the expanded strip's Bias knob.
  Identity transform plus:

    1. Band area (editor template, CViewContainer at 20, 190): height 350 -> 340. Its band panels are 340 tall and its
       expanded strips 290, so nothing inside moves; the 10 px freed above the sweep row make room for row 2 below.
    2. Sweep row (editor template, CViewContainer at 0, 550, 1000 x 50): moved to 0, 530 and made 1000 x 70, so the
       editor keeps its 1000 x 600 default size (the mod panel still starts at 600). Line 1 (every existing control)
       moves up 4 px: toggles y 12 -> 8, labels / sliders y 15 -> 11. Line 2 is new, each control under the group it
       belongs to (toggles / knobs y 39, 25 tall; labels / menus / segments y 41, 20 tall):
         x  20 "Intensity" label   x  70 ArcKnob        SweepIntensity       (under Sweep / Enable)
         x 110 "Falloff"   label   x 150 CSegmentButton SweepFalloff  Hard, Soft   (under Freq)
         x 260 "Link"      label   x 300 COptionMenu    SweepMorphLink       (under Width)
         x 400 "Wave"      label   x 430 COptionMenu    SweepLFOWaveform     (under LFO / Rate)
         x 500 "Sync"      label   x 525 ToggleButton   SweepLFOSync
         x 555 "Note"      label   x 585 COptionMenu    SweepLFONoteValue    (under Depth)
         x 650 "Sens"      label   x 680 ArcKnob        SweepEnvSensitivity  (under Env / Enable)
       Classes and attributes are copied from the neighbours: toggles and sliders from this row, menus from the
       header's Bands / OS Max menus (same background-secondary strip), the segment button from SpectrumMode, the
       knobs from the expanded strip's Smooth knob (30 x 25, accent-secondary like this row's slider values).
    3. BandExpandedStrip header: a Bias label + ArcKnob (Band.NodeBias, the selected node's bias proxy) after Mix,
       same geometry and colours as Drive / Mix (label 40 x 12 at x 330, knob 30 x 24 at x 335).

  Apply:  npx xslt3 -xsl:specs/disrumpo-audit/xslt/t222-sweep-controls.xsl
                    -s:plugins/disrumpo/resources/editor.uidesc -o:OUT
  then restore the file's own formatting on OUT, which Saxon's serializer changes: the UTF-8 BOM, CRLF line ends,
  no final line end, each comment on its own line (the indenter glues it to the next element) and the empty
  <bitmaps> element written open / close. With that, `git diff` shows only the three changes above.
-->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:saxon="http://saxon.sf.net/"
                exclude-result-prefixes="saxon">

  <xsl:output method="xml" encoding="UTF-8" indent="yes" saxon:indent-spaces="2" saxon:line-length="100000"/>
  <xsl:strip-space elements="*"/>
  <xsl:mode on-no-match="shallow-copy"/>

  <!-- 1. band area: 350 -> 340 tall -->
  <xsl:template match="template[@name='editor']/view[@class='CViewContainer' and @origin='20, 190']/@size">
    <xsl:attribute name="size" select="'960, 340'"/>
  </xsl:template>

  <!-- 2. sweep row: new origin / size, line 1 up 4 px, line 2 appended -->
  <xsl:template match="template[@name='editor']/view[@class='CViewContainer' and @origin='0, 550']">
    <xsl:copy>
      <xsl:apply-templates select="@*"/>
      <xsl:attribute name="origin" select="'0, 530'"/>
      <xsl:attribute name="size" select="'1000, 70'"/>
      <xsl:apply-templates select="view" mode="line1"/>
      <!-- line 2 -->
      <view class="CTextLabel" origin="20, 41" size="50, 20" title="Intensity" font="small-font" font-color="text-secondary"/>
      <view class="ArcKnob" origin="70, 39" size="30, 25" control-tag="SweepIntensity" default-value="0.25" min-value="0" max-value="1" arc-color="accent-secondary" tooltip="Sweep intensity (0-200 %)"/>
      <view class="CTextLabel" origin="110, 41" size="40, 20" title="Falloff" font="small-font" font-color="text-secondary"/>
      <view class="CSegmentButton" origin="150, 41" size="100, 20" control-tag="SweepFalloff" default-value="1" style="horizontal" segment-names="Hard, Soft" font="small-font" font-color="text-primary" frame-color="border" gradient="button-default" selected-gradient="button-active"/>
      <view class="CTextLabel" origin="260, 41" size="40, 20" title="Link" font="small-font" font-color="text-secondary"/>
      <view class="COptionMenu" origin="300, 41" size="80, 20" control-tag="SweepMorphLink" default-value="0" font="value-font" font-color="text-primary" back-color="panel" frame-color="border" tooltip="Sweep-to-morph link curve"/>
      <view class="CTextLabel" origin="400, 41" size="30, 20" title="Wave" font="small-font" font-color="text-secondary"/>
      <view class="COptionMenu" origin="430, 41" size="65, 20" control-tag="SweepLFOWaveform" default-value="0" font="value-font" font-color="text-primary" back-color="panel" frame-color="border" tooltip="Sweep LFO waveform"/>
      <view class="CTextLabel" origin="500, 41" size="25, 20" title="Sync" font="small-font" font-color="text-secondary"/>
      <view class="ToggleButton" origin="525, 39" size="25, 25" on-color="accent-primary" off-color="text-secondary" control-tag="SweepLFOSync" default-value="0" tooltip="Sync sweep LFO to tempo"/>
      <view class="CTextLabel" origin="555, 41" size="30, 20" title="Note" font="small-font" font-color="text-secondary"/>
      <view class="COptionMenu" origin="585, 41" size="55, 20" control-tag="SweepLFONoteValue" default-value="0" font="value-font" font-color="text-primary" back-color="panel" frame-color="border" tooltip="Sweep LFO note value (Sync on)"/>
      <view class="CTextLabel" origin="650, 41" size="30, 20" title="Sens" font="small-font" font-color="text-secondary"/>
      <view class="ArcKnob" origin="680, 39" size="30, 25" control-tag="SweepEnvSensitivity" default-value="0.5" min-value="0" max-value="1" arc-color="accent-secondary" tooltip="Sweep envelope sensitivity"/>
    </xsl:copy>
  </xsl:template>

  <!-- line 1: toggles at y 12 and labels / sliders at y 15 move up 4 px -->
  <xsl:template match="view" mode="line1">
    <xsl:copy>
      <xsl:apply-templates select="@*"/>
      <xsl:variable name="xy" select="tokenize(@origin, ',\s*')"/>
      <xsl:attribute name="origin" select="concat($xy[1], ', ', number($xy[2]) - 4)"/>
    </xsl:copy>
  </xsl:template>

  <!-- 3. expanded strip header: Bias after Mix -->
  <xsl:template match="template[@name='BandExpandedStrip']/view[1]/view[@class='ArcKnob' and @control-tag='Band.NodeMix']">
    <xsl:copy-of select="."/>
    <view class="CTextLabel" origin="330, 2" size="40, 12" title="Bias" font="small-font" font-color="text-secondary" text-alignment="center"/>
    <view class="ArcKnob" origin="335, 14" size="30, 24" control-tag="Band.NodeBias" default-value="0.5" min-value="0" max-value="1" arc-color="accent-primary"/>
  </xsl:template>
</xsl:stylesheet>
