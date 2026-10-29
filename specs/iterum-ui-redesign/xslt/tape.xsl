<?xml version="1.0" encoding="UTF-8"?>
<!-- tape.xsl - rebuilds TapePanel per specs/iterum-ui-redesign/panels/tape.md (identity for everything else). -->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                exclude-result-prefixes="xs">
  <xsl:output method="xml" indent="yes" encoding="UTF-8"/>
  <xsl:include href="cells.xsl"/>

  <xsl:mode on-no-match="shallow-copy"/>

  <!-- Tempo sync (Time Mode 419, Note Value 420), after the last Tape tag -->
  <xsl:template match="control-tag[@name = 'TapeHead3Pan']">
    <xsl:copy-of select="."/>
    <xsl:if test="not(../control-tag[@name = 'TapeTimeMode'])">
      <control-tag name="TapeTimeMode" tag="419"/>
      <control-tag name="TapeNoteValue" tag="420"/>
    </xsl:if>
  </xsl:template>

  <xsl:template match="template[@name = 'TapePanel']">
    <template name="TapePanel" class="CViewContainer" origin="0, 0" size="900, 480" background-color="panel"
              transparent="false">
      <!-- core strip -->
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="tag" select="'TapeMotorSpeed'"/>
        <xsl:with-param name="label" select="'Time'"/>
        <xsl:with-param name="sub-controller" select="'TapeTimeSyncController'"/>
        <xsl:with-param name="toggle-tag" select="'TapeTimeMode'"/>
        <xsl:with-param name="toggle-title" select="'Sync'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="tag" select="'TapeFeedback'"/>
        <xsl:with-param name="label" select="'Feedback'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="tag" select="'TapeMix'"/>
        <xsl:with-param name="label" select="'Mix'"/>
      </xsl:call-template>

      <!-- MOTOR -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'MOTOR'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'TapeMotorInertia'"/>
            <xsl:with-param name="label" select="'Inertia'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'TapeWear'"/>
            <xsl:with-param name="label" select="'Wear'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- CHARACTER -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'CHARACTER'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'TapeSaturation'"/>
            <xsl:with-param name="label" select="'Saturation'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'TapeAge'"/>
            <xsl:with-param name="label" select="'Age'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- SPLICE -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'SPLICE'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="toggle-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="124"/>
            <xsl:with-param name="tag" select="'TapeSpliceEnabled'"/><xsl:with-param name="opens-section" select="true()"/>
            <xsl:with-param name="label" select="'Splice'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'TapeSpliceIntensity'"/>
            <xsl:with-param name="label" select="'Splice Intensity'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- HEADS -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="y" select="288"/>
        <xsl:with-param name="w" select="876"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'HEADS'"/>
        <xsl:with-param name="content">
          <xsl:for-each select="1 to 3">
            <xsl:variable name="n" select="."/>
            <xsl:variable name="row" select="24 + ($n - 1) * 52"/>
            <xsl:call-template name="toggle-cell">
              <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="$row"/>
              <xsl:with-param name="w" select="120"/>
              <xsl:with-param name="tag" select="concat('TapeHead', $n, 'Enabled')"/>
              <xsl:with-param name="label" select="concat('Head ', $n)"/>
            </xsl:call-template>
            <!-- Toggle 120, then Level and Pan share the rest with the 24-pt multi-column gutter (12 + 120 + 24 +
                 342 + 24 + 342 + 12 = 876): no hole after the toggle, and a readout never abuts the next label. -->
            <xsl:call-template name="slider-cell">
              <xsl:with-param name="x" select="156"/><xsl:with-param name="y" select="$row"/>
              <xsl:with-param name="w" select="342"/>
              <xsl:with-param name="tag" select="concat('TapeHead', $n, 'Level')"/>
              <xsl:with-param name="label" select="'Level'"/>
            </xsl:call-template>
            <xsl:call-template name="bipolar-cell">
              <xsl:with-param name="x" select="522"/><xsl:with-param name="y" select="$row"/>
              <xsl:with-param name="w" select="342"/>
              <xsl:with-param name="tag" select="concat('TapeHead', $n, 'Pan')"/>
              <xsl:with-param name="label" select="'Pan'"/>
            </xsl:call-template>
          </xsl:for-each>
        </xsl:with-param>
      </xsl:call-template>
    </template>
  </xsl:template>
</xsl:stylesheet>
