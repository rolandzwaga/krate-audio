<?xml version="1.0" encoding="UTF-8"?>
<!-- granular.xsl - rebuilds template GranularPanel per specs/iterum-ui-redesign/panels/granular.md. Identity otherwise. -->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                exclude-result-prefixes="xs">
  <xsl:include href="cells.xsl"/>
  <xsl:output method="xml" indent="yes" encoding="UTF-8"/>

  <xsl:mode on-no-match="shallow-copy"/>

  <xsl:template match="template[@name = 'GranularPanel']">
    <template name="GranularPanel" class="CViewContainer" origin="0, 0" size="900, 480" background-color="panel"
              transparent="false">
      <!-- core strip -->
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="tag" select="'GranularDelayTime'"/>
        <xsl:with-param name="label" select="'Time'"/>
        <xsl:with-param name="sub-controller" select="'GranularTimeSyncController'"/>
        <xsl:with-param name="toggle-tag" select="'GranularTimeMode'"/>
        <xsl:with-param name="toggle-title" select="'Sync'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="tag" select="'GranularFeedback'"/>
        <xsl:with-param name="label" select="'Feedback'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="tag" select="'GranularDryWet'"/>
        <xsl:with-param name="label" select="'Mix'"/>
        <xsl:with-param name="toggle-tag" select="'GranularFreeze'"/>
        <xsl:with-param name="toggle-title" select="'Freeze'"/>
      </xsl:call-template>

      <!-- GRAIN -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/><xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'GRAIN'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'GranularGrainSize'"/><xsl:with-param name="label" select="'Grain Size'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'GranularDensity'"/><xsl:with-param name="label" select="'Density'"/>
          </xsl:call-template>
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="128"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'GranularEnvelopeType'"/><xsl:with-param name="label" select="'Grain Envelope'"/>
            <xsl:with-param name="segments" select="'Hann,Trapezoid,Sine,Blackman'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- PITCH -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="308"/><xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/><xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'PITCH'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="bipolar-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'GranularPitch'"/><xsl:with-param name="label" select="'Pitch'"/>
          </xsl:call-template>
          <xsl:call-template name="menu-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'GranularPitchQuant'"/><xsl:with-param name="label" select="'Pitch Quantize'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="128"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'GranularPitchSpray'"/><xsl:with-param name="label" select="'Pitch Spray'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- SPRAY -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="604"/><xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/><xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'SPRAY'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'GranularPositionSpray'"/><xsl:with-param name="label" select="'Position Spray'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'GranularPanSpray'"/><xsl:with-param name="label" select="'Pan Spray'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="128"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'GranularReverseProb'"/><xsl:with-param name="label" select="'Reverse Probability'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- CHARACTER -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="288"/>
        <xsl:with-param name="w" select="284"/><xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'CHARACTER'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'GranularTexture'"/><xsl:with-param name="label" select="'Texture'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'GranularJitter'"/><xsl:with-param name="label" select="'Timing Jitter'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- STEREO (pinned column 3; cell at 308,288 stays empty) -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="604"/><xsl:with-param name="y" select="288"/>
        <xsl:with-param name="w" select="284"/><xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'STEREO'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'GranularStereoWidth'"/><xsl:with-param name="label" select="'Width'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>
    </template>
  </xsl:template>
</xsl:stylesheet>
