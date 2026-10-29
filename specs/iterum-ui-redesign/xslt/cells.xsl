<?xml version="1.0" encoding="UTF-8"?>
<!--
  cells.xsl - the cell vocabulary of specs/iterum-ui-redesign/design-system.md section 4, as named templates.
  The ten panel stylesheets <xsl:include href="cells.xsl"/> it and only say which cell goes where; one place
  defines the geometry and the attribute sets (design-system.md 9.2 step 2).

  Coordinates: x / y are the cell origin in its parent (the core strip cells: panel coordinates, y fixed at 12;
  body cells: fieldset coordinates, rows y = 24, 76, 128, 180, 232, 284); w is the cell width (260, 124, 556, 272,
  852, 420, 276, 204). No cell emits default-value (4.1): VST3Editor sets every bound control's default from the
  parameter when it binds it.

  Templates:
    knob-cell     (x, tag, label, [sub-controller], [toggle-tag, toggle-title])   4.1, 4.9
    select-cell   (x, tag, label)                                                4.2
    slider-cell   (x, y, w, tag, label)                                          4.3
    bipolar-cell  (x, y, w, tag, label)   slider + centre tick                   4.4
    segment-cell  (x, y, w, tag, label, segments)                                4.5
    menu-cell     (x, y, w, tag, label)                                          4.6, 4.7
    toggle-cell   (x, y, w, tag, label)                                          4.8
    section       (x, y, w, h, title, content)                                   4.11
-->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                exclude-result-prefixes="xs">

  <!-- ===================================================================== 4.1 knob cell (core strip) -->
  <xsl:template name="knob-cell">
    <xsl:param name="x" as="xs:decimal" required="yes"/>
    <xsl:param name="tag" as="xs:string" required="yes"/>
    <xsl:param name="label" as="xs:string" required="yes"/>
    <xsl:param name="sub-controller" as="xs:string" select="''"/>
    <xsl:param name="toggle-tag" as="xs:string" select="''"/>
    <xsl:param name="toggle-title" as="xs:string" select="''"/>
    <view class="CViewContainer" origin="{$x}, 12" size="284, 72" transparent="true">
      <xsl:if test="$sub-controller != ''">
        <xsl:attribute name="sub-controller" select="$sub-controller"/>
      </xsl:if>
      <view class="ArcKnob" origin="8, 8" size="56, 56" control-tag="{$tag}" arc-color="accent"
            guide-color="slider-frame" arc-width="3"/>
      <view class="CTextLabel" origin="76, 10" size="120, 16" title="{$label}" font="label-font"
            font-color="text-dim" text-alignment="left" transparent="true"/>
      <view class="CParamDisplay" origin="76, 30" size="120, 24" control-tag="{$tag}" font="readout-font"
            font-color="text" text-alignment="left" back-color="~ TransparentCColor"
            frame-color="~ TransparentCColor" style-no-frame="true"/>
      <!-- The toggle slot: Sync beside Time, Freeze beside Mix. Declared after the knob and the readout. -->
      <xsl:if test="$toggle-tag != ''">
        <view class="ToggleButton" origin="200, 32" size="76, 22" control-tag="{$toggle-tag}" title="{$toggle-title}"
              title-position="right" icon-style="power" icon-size="0.6" font="label-font" on-color="accent"
              off-color="text-dim"/>
      </xsl:if>
    </view>
  </xsl:template>

  <!-- ===================================================================== 4.2 select cell (core strip) -->
  <xsl:template name="select-cell">
    <xsl:param name="x" as="xs:decimal" required="yes"/>
    <xsl:param name="tag" as="xs:string" required="yes"/>
    <xsl:param name="label" as="xs:string" required="yes"/>
    <view class="CViewContainer" origin="{$x}, 12" size="284, 72" transparent="true">
      <view class="CTextLabel" origin="8, 10" size="200, 16" title="{$label}" font="label-font"
            font-color="text-dim" text-alignment="left" transparent="true"/>
      <xsl:call-template name="option-menu">
        <xsl:with-param name="x" select="8"/>
        <xsl:with-param name="y" select="30"/>
        <xsl:with-param name="w" select="200"/>
        <xsl:with-param name="tag" select="$tag"/>
      </xsl:call-template>
    </view>
  </xsl:template>

  <!-- ===================================================================== 4.3 slider cell -->
  <xsl:template name="slider-cell">
    <xsl:param name="x" as="xs:decimal" required="yes"/>
    <xsl:param name="y" as="xs:decimal" required="yes"/>
    <xsl:param name="w" as="xs:decimal" required="yes"/>
    <xsl:param name="tag" as="xs:string" required="yes"/>
    <xsl:param name="label" as="xs:string" required="yes"/>
    <xsl:param name="bipolar" as="xs:boolean" select="false()"/>
    <view class="CTextLabel" origin="{$x}, {$y}" size="{$w - 64}, 16" title="{$label}" font="label-font"
          font-color="text-dim" text-alignment="left" transparent="true"/>
    <view class="CParamDisplay" origin="{$x + $w - 64}, {$y}" size="64, 16" control-tag="{$tag}" font="value-font"
          font-color="text" text-alignment="right" back-color="~ TransparentCColor"
          frame-color="~ TransparentCColor" style-no-frame="true"/>
    <view class="CSlider" origin="{$x}, {$y + 22}" size="{$w}, 20" control-tag="{$tag}" orientation="horizontal"
          draw-frame="true" draw-back="true" draw-value="true" draw-frame-color="slider-frame"
          draw-back-color="slider-track" draw-value-color="accent" frame-width="1" transparent="true">
      <xsl:if test="$bipolar">
        <xsl:attribute name="draw-value-from-center" select="'true'"/>
      </xsl:if>
    </view>
  </xsl:template>

  <!-- ===================================================================== 4.4 bipolar cell (+ centre tick) -->
  <xsl:template name="bipolar-cell">
    <xsl:param name="x" as="xs:decimal" required="yes"/>
    <xsl:param name="y" as="xs:decimal" required="yes"/>
    <xsl:param name="w" as="xs:decimal" required="yes"/>
    <xsl:param name="tag" as="xs:string" required="yes"/>
    <xsl:param name="label" as="xs:string" required="yes"/>
    <xsl:call-template name="slider-cell">
      <xsl:with-param name="x" select="$x"/>
      <xsl:with-param name="y" select="$y"/>
      <xsl:with-param name="w" select="$w"/>
      <xsl:with-param name="tag" select="$tag"/>
      <xsl:with-param name="label" select="$label"/>
      <xsl:with-param name="bipolar" select="true()"/>
    </xsl:call-template>
    <!-- Over the track (declared after the slider: CSlider paints its track opaque), mouse-disabled so the slider
         under it takes every click; no control-tag. -->
    <view class="CViewContainer" origin="{$x + $w div 2}, {$y + 22}" size="1, 20" background-color="text-dim"
          transparent="false" mouse-enabled="false"/>
  </xsl:template>

  <!-- ===================================================================== 4.5 segment cell -->
  <xsl:template name="segment-cell">
    <xsl:param name="x" as="xs:decimal" required="yes"/>
    <xsl:param name="y" as="xs:decimal" required="yes"/>
    <xsl:param name="w" as="xs:decimal" required="yes"/>
    <xsl:param name="tag" as="xs:string" required="yes"/>
    <xsl:param name="label" as="xs:string" required="yes"/>
    <xsl:param name="segments" as="xs:string" required="yes"/>
    <xsl:call-template name="cell-label">
      <xsl:with-param name="x" select="$x"/>
      <xsl:with-param name="y" select="$y"/>
      <xsl:with-param name="w" select="$w"/>
      <xsl:with-param name="label" select="$label"/>
    </xsl:call-template>
    <view class="CSegmentButton" origin="{$x}, {$y + 20}" size="{$w}, 24" control-tag="{$tag}" style="horizontal"
          selection-mode="kSingle" segment-names="{$segments}" font="value-font" text-color="text"
          text-color-highlighted="text-light" frame-color="border" frame-width="1" round-radius="3"
          gradient="segment-bg" gradient-highlighted="segment-bg-selected"/>
  </xsl:template>

  <!-- ===================================================================== 4.6 menu cell -->
  <xsl:template name="menu-cell">
    <xsl:param name="x" as="xs:decimal" required="yes"/>
    <xsl:param name="y" as="xs:decimal" required="yes"/>
    <xsl:param name="w" as="xs:decimal" required="yes"/>
    <xsl:param name="tag" as="xs:string" required="yes"/>
    <xsl:param name="label" as="xs:string" required="yes"/>
    <xsl:call-template name="cell-label">
      <xsl:with-param name="x" select="$x"/>
      <xsl:with-param name="y" select="$y"/>
      <xsl:with-param name="w" select="$w"/>
      <xsl:with-param name="label" select="$label"/>
    </xsl:call-template>
    <xsl:call-template name="option-menu">
      <xsl:with-param name="x" select="$x"/>
      <xsl:with-param name="y" select="$y + 20"/>
      <xsl:with-param name="w" select="$w"/>
      <xsl:with-param name="tag" select="$tag"/>
    </xsl:call-template>
  </xsl:template>

  <!-- ===================================================================== 4.8 toggle cell -->
  <xsl:template name="toggle-cell">
    <xsl:param name="x" as="xs:decimal" required="yes"/>
    <xsl:param name="y" as="xs:decimal" required="yes"/>
    <xsl:param name="w" as="xs:decimal" required="yes"/>
    <xsl:param name="tag" as="xs:string" required="yes"/>
    <xsl:param name="label" as="xs:string" required="yes"/>
    <!-- opens-section: the toggle is a section's first row with nothing beside it, so it takes the label row itself
         (y, not y + 20) and the section has no hole at the top. Off where a toggle shares its row with other cells
         (Tape HEADS), so it stays level with their controls. -->
    <xsl:param name="opens-section" as="xs:boolean" select="false()"/>
    <!-- Otherwise the label row is empty: the toggle's title is its label. -->
    <view class="ToggleButton" origin="{$x}, {if ($opens-section) then $y else $y + 20}" size="{$w}, 24" control-tag="{$tag}" title="{$label}"
          title-position="right" icon-style="power" icon-size="0.6" font="label-font" on-color="accent"
          off-color="text-dim"/>
  </xsl:template>

  <!-- ===================================================================== 4.11 section container -->
  <!-- content: the section's cells, e.g. <xsl:with-param name="content"><xsl:call-template name="slider-cell">...
       </xsl:with-param>. Cells are direct children of the fieldset (8.1: the container visibility controllers
       toggle an anchor control's IMMEDIATE parent). -->
  <xsl:template name="section">
    <xsl:param name="x" as="xs:decimal" required="yes"/>
    <xsl:param name="y" as="xs:decimal" required="yes"/>
    <xsl:param name="w" as="xs:decimal" required="yes"/>
    <xsl:param name="h" as="xs:decimal" required="yes"/>
    <xsl:param name="title" as="xs:string" required="yes"/>
    <xsl:param name="content" as="item()*" select="()"/>
    <view class="FieldsetContainer" origin="{$x}, {$y}" size="{$w}, {$h}" fieldset-title="{$title}"
          fieldset-color="border" fieldset-title-color="text-dim" fieldset-font-size="11" fieldset-radius="4"
          fieldset-line-width="1" transparent="true">
      <xsl:copy-of select="$content"/>
    </view>
  </xsl:template>

  <!-- ===================================================================== shared parts -->
  <xsl:template name="cell-label">
    <xsl:param name="x" as="xs:decimal" required="yes"/>
    <xsl:param name="y" as="xs:decimal" required="yes"/>
    <xsl:param name="w" as="xs:decimal" required="yes"/>
    <xsl:param name="label" as="xs:string" required="yes"/>
    <view class="CTextLabel" origin="{$x}, {$y}" size="{$w}, 16" title="{$label}" font="label-font"
          font-color="text-dim" text-alignment="left" transparent="true"/>
  </xsl:template>

  <!-- 4.7 the menu attribute set. -->
  <xsl:template name="option-menu">
    <xsl:param name="x" as="xs:decimal" required="yes"/>
    <xsl:param name="y" as="xs:decimal" required="yes"/>
    <xsl:param name="w" as="xs:decimal" required="yes"/>
    <xsl:param name="tag" as="xs:string" required="yes"/>
    <view class="COptionMenu" origin="{$x}, {$y}" size="{$w}, 24" control-tag="{$tag}" font="value-font"
          font-color="text" back-color="slider-track" frame-color="border" frame-width="1" style-round-rect="true"
          round-rect-radius="3" text-inset="6, 0" menu-popup-style="true" mouse-enabled="true" wants-focus="true"/>
  </xsl:template>

</xsl:stylesheet>
