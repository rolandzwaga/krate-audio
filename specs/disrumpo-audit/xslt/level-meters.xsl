<?xml version="1.0" encoding="UTF-8"?>
<!--
  level-meters.xsl - level meters (user ruling, 2026-11-09). Identity transform plus:

    1. Colour table: meter-low (#4ECDC4, the theme's accent-secondary), meter-mid (#FFCB6B, warning yellow) and
       meter-clip (#FF5370, red) appended after "shadow". LevelMeterView fills below -18 dBFS with meter-low, -18 ... 0
       with meter-mid, at or above 0 and a latched clip with meter-clip.
    2. Header bar (editor template, CViewContainer at 0, 0, 1000 x 50), in the gap between the VersionLabel (ends at
       x 626) and the knob group (starts at x 700), after the VersionLabel:
         InputLevelMeter   origin 636, 2   size 14 x 36  (clip LED 6 x 4 on top, L / R bars 6 x 30 below)
         "IN"  label       origin 630, 38  size 26 x 12  small-font text-secondary
         OutputLevelMeter  origin 670, 2   size 14 x 36
         "OUT" label       origin 664, 38  size 26 x 12
       Everything inside x 630 ... 690.
    3. Band panels (CViewContainers with control-tag Band1Container ... Band4Container): after the "Band N" title
       label (origin 0, 5, 200 x 20), BandNLevelMeter at origin 0, 25, 200 x 4: the band's level bar in band-N with a
       6 x 4 clip dot at its right end. The "Type" label below starts at y 30.

  Apply:  npx xslt3 -xsl:specs/disrumpo-audit/xslt/level-meters.xsl
                    -s:plugins/disrumpo/resources/editor.uidesc -o:OUT
  then restore the file's own formatting on OUT exactly as for t222-sweep-controls.xsl (UTF-8 BOM, CRLF line ends,
  no final line end, each comment on its own line, the empty <bitmaps> element written open / close).
-->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:saxon="http://saxon.sf.net/"
                exclude-result-prefixes="saxon">

  <xsl:output method="xml" encoding="UTF-8" indent="yes" saxon:indent-spaces="2" saxon:line-length="100000"/>
  <xsl:strip-space elements="*"/>
  <xsl:mode on-no-match="shallow-copy"/>

  <!-- 1. colour table -->
  <xsl:template match="colors/color[@name = 'shadow']">
    <xsl:copy-of select="."/>
    <color name="meter-low" rgba="#4ECDC4ff"/>
    <color name="meter-mid" rgba="#FFCB6Bff"/>
    <color name="meter-clip" rgba="#FF5370ff"/>
  </xsl:template>

  <!-- 2. header: IN / OUT meters after the version label -->
  <xsl:template match="template[@name = 'editor']/view[@class = 'CViewContainer' and @origin = '0, 0' and @size = '1000, 50']/view[@custom-view-name = 'VersionLabel']">
    <xsl:if test="@origin ne '560, 17' or @size ne '66, 16'">
      <xsl:message terminate="yes">VersionLabel is at <xsl:value-of select="@origin"/> / <xsl:value-of select="@size"/>, expected 560, 17 / 66, 16</xsl:message>
    </xsl:if>
    <xsl:copy-of select="."/>
    <view custom-view-name="InputLevelMeter" origin="636, 2" size="14, 36" tooltip="Input level after Input Gain (click to clear the clip light)"/>
    <view class="CTextLabel" origin="630, 38" size="26, 12" title="IN" font="small-font" font-color="text-secondary" text-alignment="center" transparent="true" mouse-enabled="false"/>
    <view custom-view-name="OutputLevelMeter" origin="670, 2" size="14, 36" tooltip="Output level (click to clear the clip light)"/>
    <view class="CTextLabel" origin="664, 38" size="26, 12" title="OUT" font="small-font" font-color="text-secondary" text-alignment="center" transparent="true" mouse-enabled="false"/>
  </xsl:template>

  <!-- 3. band panels: a level bar under the title -->
  <xsl:template match="template[@name = 'editor']//view[@class = 'CViewContainer' and matches(@control-tag, '^Band[1-4]Container$')]/view[@class = 'CTextLabel' and @origin = '0, 5']">
    <xsl:variable name="n" select="substring(../@control-tag, 5, 1)"/>
    <xsl:if test="@size ne '200, 20' or @title ne concat('Band ', $n)">
      <xsl:message terminate="yes">Band <xsl:value-of select="$n"/> title is <xsl:value-of select="@title"/> / <xsl:value-of select="@size"/>, expected Band <xsl:value-of select="$n"/> / 200, 20</xsl:message>
    </xsl:if>
    <xsl:copy-of select="."/>
    <view custom-view-name="Band{$n}LevelMeter" origin="0, 25" size="200, 4" tooltip="Band {$n} level (click to clear the clip light)"/>
  </xsl:template>

</xsl:stylesheet>
